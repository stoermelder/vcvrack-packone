#include "MidiKit.test.hpp"

// Self-tests for the harness at the end of MidiKit.test.hpp (var/MidiKit_test_review.md §7).
// They pin what the rig does, so a test migrated onto it can trust a green or red result.

TEST_CASE("Harness: msg builders produce the wire bytes of the MIDI spec", "[MidiKit][Harness]") {
	REQUIRE(msg::noteOn(2, 60, 100).bytes[0] == 0x92);
	REQUIRE(msg::noteOff(2, 60).bytes[2] == 0);
	REQUIRE(msg::cc(1, 7, 99).getSize() == 3);
	REQUIRE(msg::programChange(3, 5).getSize() == 2);
	REQUIRE(msg::clock().bytes[0] == 0xf8);

	// 14-bit pitch wheel: LSB first.
	midi::Message pw = msg::pitchWheel(0, 0x2005);
	REQUIRE(pw.bytes[1] == 0x05);
	REQUIRE(pw.bytes[2] == 0x40);

	auto n = msg::nrpn(0, 0x0102, 0x0383);
	REQUIRE(n.size() == 4);
	REQUIRE(n[0].bytes[1] == 99);  REQUIRE(n[0].bytes[2] == 2);
	REQUIRE(n[1].bytes[1] == 98);  REQUIRE(n[1].bytes[2] == 2);
	REQUIRE(n[2].bytes[1] == 6);   REQUIRE(n[2].bytes[2] == 7);
	REQUIRE(n[3].bytes[1] == 38);  REQUIRE(n[3].bytes[2] == 3);
	REQUIRE(msg::rpn(0, 0, 1)[0].bytes[1] == 101);

	auto c14 = msg::cc14(0, 1, 0x0fff);
	REQUIRE(c14[0].bytes[1] == 1);   REQUIRE(c14[0].bytes[2] == 0x1f);
	REQUIRE(c14[1].bytes[1] == 33);  REQUIRE(c14[1].bytes[2] == 0x7f);

	auto r = msg::releasesOf(0, 60);
	REQUIRE(r[0].bytes[0] == 0x80);
	REQUIRE(r[1].bytes[0] == 0x90);
	REQUIRE(r[1].bytes[2] == 0);

	REQUIRE(msg::raw({0xf0, 0x7d, 0xf7}).getSize() == 3);
}

TEST_CASE("Harness: script builders and stmt() translate JS statements to Lua", "[MidiKit][Harness]") {
	REQUIRE(header(Lang::Js).find("QuickJs") != std::string::npos);
	REQUIRE(header(Lang::Lua, "@requires messages=64").find("@requires messages=64") != std::string::npos);
	REQUIRE(stmt(Lang::Lua, "let a = 1; const b = 2;") == "local a = 1 local b = 2");
	REQUIRE(stmt(Lang::Js, "let a = 1;") == "let a = 1;");
	REQUIRE(countOf("abcabcabc", "abc") == 3);
	REQUIRE(lines("a\nb\n").size() == 2);
	REQUIRE(lines("a\n\nb").size() == 3);

	std::string timed = withTiming(EMPTY(Lang::Lua));
	REQUIRE(timed.find("midiOut.enableTiming()\n") != std::string::npos);
	REQUIRE(withTiming(EMPTY(Lang::Js)).find("midiOut.enableTiming();") != std::string::npos);
}

TEST_CASE("Harness: Kit loads a script and the engine layer returns what it sent", "[MidiKit][Harness]") {
	FOR_EACH_LANG;
	Kit<> k;
	k.load(onMessage(lang, "midiOut.send(msg);"));
	auto out = k.dispatch(msg::noteOn(3, 61, 90));
	REQUIRE(out.size() == 1);
	REQUIRE(out[0] == Out::want(0x9, 3, 61, 90));
	REQUIRE(out[0].bytes == std::vector<uint8_t>{0x93, 61, 90});
	REQUIRE(k.drain().empty());
	k.requireNoError();
}

TEST_CASE("Harness: Kit::load fails on a script that does not load, loadRaw reports it", "[MidiKit][Harness]") {
	Kit<> k;
	std::string log = k.loadRaw(script(Lang::Js, "this is not javascript ("));
	REQUIRE(log.find("rror") != std::string::npos);
}

TEST_CASE("Harness: Kit::probes returns the P: lines of the log", "[MidiKit][Harness]") {
	FOR_EACH_LANG;
	Kit<> k;
	k.load(onMessage(lang, lang == Lang::Js
		? "rack.log('P:' + midi.getNote(msg)); rack.log('other');"
		: "rack.log('P:' .. midi.getNote(msg)) rack.log('other')"));
	k.dispatch(msg::noteOn(0, 64, 1));
	REQUIRE(k.probes() == std::vector<std::string>{"64"});
}

TEST_CASE("Harness: the module layer decodes injected messages after the divider", "[MidiKit][Harness]") {
	FOR_EACH_LANG;
	DeviceKit<> k;
	k.load(onMessage(lang, "midiOut.send(msg);"));
	k.inject(msg::noteOn(0, 60, 100), 20);
	k.runUntil(10);
	REQUIRE(k.dev[0].sent.empty());   // not due yet
	k.runUntil(40);
	REQUIRE(k.dev[0].sent.size() == 1);
	REQUIRE(k.dev[0].notes(0x9) == std::vector<int>{60});
	REQUIRE(k.dev[0].sent[0].releasedAt >= 20);
	REQUIRE(k.dev[0].count(0x9) == 1);
	REQUIRE(k.frame == 40);
}

TEST_CASE("Harness: trig() and pulse() drive one channel of a trigger input", "[MidiKit][Harness]") {
	FOR_EACH_LANG;
	Kit<> k;
	k.load(script(lang, lang == Lang::Js
		? "trig.enableIn(1, 1); trig.onTrigger = function(port, ch) { rack.log('P:' + port + ':' + ch + ':' + trig.getTicks(port, ch)); };"
		: "trig.enableIn(1, 1) trig.onTrigger = function(port, ch) rack.log('P:' .. port .. ':' .. ch .. ':' .. trig.getTicks(port, ch)) end"));
	k.pumpDivider();   // primes the channel LOW
	k.pulse();
	k.pulse();
	auto p = k.probes();
	REQUIRE(p.size() == 2);
	REQUIRE(p[0] != p[1]);
}

TEST_CASE("Harness: runBoth pins the bytes both engines send", "[MidiKit][Harness]") {
	requireBytes(runBoth("let m = midi.create(); midi.setNoteOn(m, 1, 60, 100); midiOut.send(m);",
	                     "local m = midi.create(); midi.setNoteOn(m, 1, 60, 100); midiOut.send(m)"),
	             {{0x90, 60, 100}});
}

TEST_CASE("Harness: requireSame compares ticks, requireLogged compares probes", "[MidiKit][Harness]") {
	Both b = runBoth("midiOut.sendAfterTrigger(msg, 2); rack.log('P:x');",
	                 "midiOut.sendAfterTrigger(msg, 2) rack.log('P:x')");
	requireSame(b);
	requireLogged(b, {"x"});
	REQUIRE(b.js.sent.size() == 1);
	REQUIRE(b.js.sent[0].ticks == 2);
	REQUIRE(b.lua.sent[0].ticks == 2);
}

TEST_CASE("Harness: AttachedEngine detaches its engine on scope exit", "[MidiKit][Harness]") {
	Kit<> k;
	{
		AttachedEngine<StubEngine> a(k.m);
		REQUIRE(k.m->host.getActiveEngine() == &a.eng);
		REQUIRE(k.menus().empty());
	}
	REQUIRE(k.m->host.getActiveEngine() == nullptr);
}

TEST_CASE("Harness: EngineScope installs an engine mock and restores the previous access", "[MidiKit][Harness]") {
	auto* before = StoermelderPackOne::vcv::engineAccess;
	{
		EngineScope s;
		s.mock.frame = 1234;
		REQUIRE(StoermelderPackOne::vcv::engineAccess->getFrame() == 1234);
	}
	REQUIRE(StoermelderPackOne::vcv::engineAccess == before);
}

TEST_CASE("Harness: Kit works for the other module variants", "[MidiKit][Harness]") {
	Kit<MidiKitMicroModule> k;
	k.load(onMessage(Lang::Lua, "midiOut.send(msg)"));
	REQUIRE(k.dispatch(msg::cc(0, 7, 9)).size() == 1);
}
