// Tests for BroadcastBus on bare engines: no script, no module, no worker. The
// bus only touches an engine's broadcastInQueue and its onBroadcastDropped() hook.

struct BusEngine : StubEngine {
	int dropped = 0;
	// What dispatchBroadcast() received, in order.
	std::vector<int> received;

	// `handler`: a module, when the overflow notice should reach its log.
	explicit BusEngine(StoermelderPackOne::MidiScript::MidiScriptEngineHandler* handler = nullptr) : StubEngine(handler, 1, 1, 1, 1, 1, 1) {}

	void onBroadcastDropped() override {
		dropped++;
		MidiScriptEngine::onBroadcastDropped();
	}
	void dispatchBroadcast(const StoermelderPackOne::MidiScript::InboundBroadcast& m) override {
		received.push_back((int)json_integer_value(m.value.get()));
	}
};

static std::shared_ptr<json_t> makeValue(int n) {
	return MidiScriptEngine::ownJson(json_integer(n));
}

TEST_CASE("BroadcastBus join is idempotent and leave is a no-op for strangers", "[MidiKit][BroadcastBus]") {
	StoermelderPackOne::MidiScript::BroadcastBus bus;
	BusEngine a, b;

	bus.join(&a);
	bus.join(&a);
	REQUIRE(bus.receivers.size() == 1);

	bus.leave(&b);
	REQUIRE(bus.receivers.size() == 1);

	bus.leave(&a);
	REQUIRE(bus.receivers.empty());
	bus.leave(&a);
	REQUIRE(bus.receivers.empty());
}

TEST_CASE("BroadcastBus send skips the sender and counts receivers", "[MidiKit][BroadcastBus]") {
	StoermelderPackOne::MidiScript::BroadcastBus bus;
	BusEngine a, b, c, outsider;
	bus.join(&a);
	bus.join(&b);
	bus.join(&c);

	auto value = makeValue(7);
	REQUIRE(bus.send(&a, value, 42) == 2);

	REQUIRE(a.broadcastInQueue.empty());
	REQUIRE(outsider.broadcastInQueue.empty());
	for (BusEngine* e : {&b, &c}) {
		REQUIRE(e->broadcastInQueue.size() == 1);
		StoermelderPackOne::MidiScript::InboundBroadcast m = e->broadcastInQueue.shift();
		REQUIRE(m.frame == 42);
		// Every receiver shares the sender's one conversion.
		REQUIRE(m.value.get() == value.get());
	}

	// A sender that is not a receiver reaches everyone.
	REQUIRE(bus.send(&outsider, value, -1) == 3);
	REQUIRE(a.broadcastInQueue.size() == 1);

	bus.leave(&a);
	bus.leave(&b);
	bus.leave(&c);
}

TEST_CASE("BroadcastBus send to an empty bus reaches nobody", "[MidiKit][BroadcastBus]") {
	StoermelderPackOne::MidiScript::BroadcastBus bus;
	BusEngine a;
	REQUIRE(bus.send(&a, makeValue(1), -1) == 0);
}

TEST_CASE("BroadcastBus drops the new message for a full receiver", "[MidiKit][BroadcastBus]") {
	StoermelderPackOne::MidiScript::BroadcastBus bus;
	BusEngine sender, full, roomy;
	bus.join(&full);

	const int capacity = 16;
	for (int i = 0; i < capacity; i++) {
		REQUIRE(bus.send(&sender, makeValue(i), i) == 1);
	}
	REQUIRE(full.broadcastInQueue.size() == capacity);
	REQUIRE(full.dropped == 0);

	// Only the receiver with room counts, and only the full one is told.
	bus.join(&roomy);
	REQUIRE(bus.send(&sender, makeValue(99), 99) == 1);
	REQUIRE(full.dropped == 1);
	REQUIRE(roomy.dropped == 0);
	REQUIRE(roomy.broadcastInQueue.size() == 1);

	// The queued messages were not overwritten: still 0..15 in order.
	REQUIRE(full.broadcastInQueue.size() == capacity);
	for (int i = 0; i < capacity; i++) {
		StoermelderPackOne::MidiScript::InboundBroadcast m = full.broadcastInQueue.shift();
		REQUIRE(m.frame == i);
		REQUIRE(json_integer_value(m.value.get()) == i);
	}

	bus.leave(&full);
	bus.leave(&roomy);
}

TEST_CASE("Modules built with a shared bus exchange a message", "[MidiKit][BroadcastBus]") {
	auto bus = std::make_shared<StoermelderPackOne::MidiScript::BroadcastBus>();
	auto worker = std::make_shared<StoermelderPackOne::SyncTaskWorker>();
	Kit<> kit1(worker, bus);
	Kit<> kit2(worker, bus);
	MidiKitModule* m1 = kit1.m;
	MidiKitModule* m2 = kit2.m;
	REQUIRE(m1->host.domain->bus == bus);
	REQUIRE(m2->host.domain->bus == bus);
	REQUIRE(m1->host.seLua.domain->bus == bus);
	REQUIRE(m2->host.seQuickJs.domain->bus == bus);

	// Engines joined by hand: no script defines onBroadcast yet.
	BusEngine e1(m1), e2(m2);
	e1.useBus(bus);
	e2.useBus(bus);
	bus->join(&e1);
	bus->join(&e2);

	REQUIRE(bus->send(&e1, makeValue(5), -1) == 1);
	REQUIRE(e2.received.empty());
	e2.process();
	REQUIRE(e2.received == std::vector<int>{5});
	REQUIRE(e2.broadcastInQueue.empty());
	e1.process();
	REQUIRE(e1.received.empty());
}

TEST_CASE("A full receiver logs the message overflow once per episode on its own module", "[MidiKit][BroadcastBus]") {
	auto bus = std::make_shared<StoermelderPackOne::MidiScript::BroadcastBus>();
	Kit<> kit1(std::make_shared<StoermelderPackOne::SyncTaskWorker>(), bus);
	Kit<> kit2(std::make_shared<StoermelderPackOne::SyncTaskWorker>(), bus);
	MidiKitModule* m1 = kit1.m;
	MidiKitModule* m2 = kit2.m;

	BusEngine sender(m1), receiver(m2);
	sender.useBus(bus);
	receiver.useBus(bus);
	bus->join(&receiver);

	for (int i = 0; i < 17; i++) bus->send(&sender, makeValue(i), -1);
	const std::string text = "Broadcast input queue full";
	REQUIRE(drainLog(m2).find(text) != std::string::npos);
	REQUIRE(drainLog(m1).find(text) == std::string::npos);
	receiver.process();
	REQUIRE(receiver.received.size() == 16);

	// The drain ended the episode: the next overflow is reported again.
	for (int i = 0; i < 17; i++) bus->send(&sender, makeValue(i), -1);
	std::string again = drainLog(m2);
	REQUIRE(again.find(text) != std::string::npos);
	REQUIRE(again.find(text) == again.rfind(text));
}

TEST_CASE("A destroyed engine leaves the bus", "[MidiKit][BroadcastBus]") {
	auto bus = std::make_shared<StoermelderPackOne::MidiScript::BroadcastBus>();
	{
		BusEngine e;
		e.useBus(bus);
		bus->join(&e);
		REQUIRE(bus->receivers.size() == 1);
	}
	REQUIRE(bus->receivers.empty());
}

TEST_CASE("Default-domain modules share a bus, injected-worker modules get a private one", "[MidiKit][BroadcastBus]") {
	ModuleScaffold mods;
	MidiKitModule* d1 = mods.adopt(new MidiKitModule());
	MidiKitModule* d2 = mods.adopt(new MidiKitModule());
	Kit<> tKit;
	MidiKitModule* t = tKit.m;
	REQUIRE(d1->host.domain->bus);
	REQUIRE(d1->host.domain->bus == d2->host.domain->bus);
	REQUIRE(t->host.domain->bus);
	REQUIRE(t->host.domain->bus != d1->host.domain->bus);
}

// ── Scripts: rack.sendBroadcast / rack.onBroadcast ───────────────────────────
// Modules share one bus and a SyncTaskWorker each, so ordering is deterministic:
// a broadcast is queued on the receiver and dispatched by its next process().

namespace {

std::string scriptFor(Lang lang, const std::string& body) {
	return lang == Lang::Lua
		? "--[[\n@engine minilua@v1\n--]]\n" + body
		: "/**\n * @engine QuickJs@v1\n */\n" + body;
}

// Runs the trigger callback of `m`'s script.
void fireTrigger(MidiKitModule* m) {
	m->host.getActiveEngine()->processInTick(0, 0);
	m->host.getActiveEngine()->process();
}

// A script that sends `valueExpr` when its trigger fires and logs the count.
std::string senderScript(Lang lang, const std::string& valueExpr) {
	return lang == Lang::Lua
		? scriptFor(lang, "trig.enableIn(1)\nfunction trig.onTrigger(p)\n  local n = rack.sendBroadcast(" + valueExpr + ")\n  rack.log('sent ' .. number.toString(n))\nend\n")
		: scriptFor(lang, "trig.enableIn(1);\ntrig.onTrigger = function(p) {\n  let n = rack.sendBroadcast(" + valueExpr + ");\n  rack.log('sent ' + number.toString(n));\n};\n");
}

// A script whose onBroadcast logs `logExpr`.
std::string receiverScript(Lang lang, const std::string& logExpr) {
	return lang == Lang::Lua
		? scriptFor(lang, "function rack.onBroadcast(msg)\n  rack.log(" + logExpr + ")\nend\n")
		: scriptFor(lang, "rack.onBroadcast = function(msg) {\n  rack.log(" + logExpr + ");\n};\n");
}

struct BroadcastRig {
	std::shared_ptr<StoermelderPackOne::MidiScript::BroadcastBus> bus = std::make_shared<StoermelderPackOne::MidiScript::BroadcastBus>();
	std::vector<std::unique_ptr<Kit<>>> kits;   // declared after the bus, so they go first

	MidiKitModule* create() {
		kits.emplace_back(new Kit<>(std::make_shared<StoermelderPackOne::SyncTaskWorker>(), bus));
		return kits.back()->m;
	}
	MidiKitModule* create(Lang lang, const std::string& script) {
		MidiKitModule* m = create();
		m->loadScript(scriptFor(lang, script));
		drainLog(m);
		return m;
	}
	MidiKitModule* load(Lang lang, const std::string& body) {
		MidiKitModule* m = create();
		m->loadScript(body);
		(void)lang;
		drainLog(m);
		return m;
	}
};

// The same logging expression for either engine.
std::string gotExpr(Lang lang) {
	return lang == Lang::Lua
		? "'got ' .. msg.type .. ' ' .. number.toString(msg.n) .. ' ' .. number.toString(msg.list[2]) .. ' ' .. tostring(msg.flag) .. ' ' .. number.toString(msg.nested.a.b)"
		: "'got ' + msg.type + ' ' + number.toString(msg.n) + ' ' + number.toString(msg.list[1]) + ' ' + String(msg.flag) + ' ' + number.toString(msg.nested.a.b)";
}

std::string valueFor(Lang lang) {
	return lang == Lang::Lua
		? "{type = 'transport', n = 3, list = {10, 20}, flag = true, nested = {a = {b = 7}}}"
		: "{type: 'transport', n: 3, list: [10, 20], flag: true, nested: {a: {b: 7}}}";
}

} // namespace

TEST_CASE("A broadcast reaches the receiver on its next process, between all engine pairs", "[MidiKit][Broadcast]") {
	for (Lang from : {Lang::Lua, Lang::Js}) {
		for (Lang to : {Lang::Lua, Lang::Js}) {
			BroadcastRig rig;
			MidiKitModule* receiver = rig.load(to, receiverScript(to, gotExpr(to)));
			MidiKitModule* sender = rig.load(from, senderScript(from, valueFor(from)));

			fireTrigger(sender);
			REQUIRE(drainLog(sender).find("sent 1") != std::string::npos);
			// Queued, not yet dispatched.
			REQUIRE(drainLog(receiver).empty());

			receiver->host.getActiveEngine()->process();
			REQUIRE(drainLog(receiver).find("got transport 3 20 true 7") != std::string::npos);
		}
	}
}

TEST_CASE("Only scripts that defined onBroadcast at load receive", "[MidiKit][Broadcast]") {
	for (Lang lang : {Lang::Lua, Lang::Js}) {
		BroadcastRig rig;
		// Defines the hook from a callback, after load: no effect.
		std::string late = lang == Lang::Lua
			? scriptFor(lang, "trig.enableIn(1)\nfunction trig.onTrigger(p)\n  function rack.onBroadcast(msg) rack.log('late') end\nend\n")
			: scriptFor(lang, "trig.enableIn(1);\ntrig.onTrigger = function(p) { rack.onBroadcast = function(msg) { rack.log('late'); }; };\n");
		MidiKitModule* none = rig.load(lang, scriptFor(lang, "rack.log('plain')\n"));
		MidiKitModule* lateModule = rig.load(lang, late);
		MidiKitModule* sender = rig.load(lang, senderScript(lang, "1"));
		fireTrigger(lateModule);

		fireTrigger(sender);
		REQUIRE(drainLog(sender).find("sent 0") != std::string::npos);
		none->host.getActiveEngine()->process();
		lateModule->host.getActiveEngine()->process();
		REQUIRE(drainLog(none).find("late") == std::string::npos);
		REQUIRE(drainLog(lateModule).find("late") == std::string::npos);
	}
}

TEST_CASE("A sender does not receive its own broadcast", "[MidiKit][Broadcast]") {
	for (Lang lang : {Lang::Lua, Lang::Js}) {
		BroadcastRig rig;
		std::string both = lang == Lang::Lua
			? scriptFor(lang, "trig.enableIn(1)\nfunction rack.onBroadcast(msg) rack.log('echo') end\nfunction trig.onTrigger(p)\n  rack.log('sent ' .. number.toString(rack.sendBroadcast(1)))\nend\n")
			: scriptFor(lang, "trig.enableIn(1);\nrack.onBroadcast = function(msg) { rack.log('echo'); };\ntrig.onTrigger = function(p) { rack.log('sent ' + number.toString(rack.sendBroadcast(1))); };\n");
		MidiKitModule* m = rig.load(lang, both);
		fireTrigger(m);
		m->host.getActiveEngine()->process();
		std::string log = drainLog(m);
		REQUIRE(log.find("sent 0") != std::string::npos);
		REQUIRE(log.find("echo") == std::string::npos);
	}
}

TEST_CASE("A bypassed module still receives broadcasts", "[MidiKit][Broadcast]") {
	BroadcastRig rig;
	MidiKitModule* receiver = rig.load(Lang::Lua, receiverScript(Lang::Lua, gotExpr(Lang::Lua)));
	MidiKitModule* sender = rig.load(Lang::Js, senderScript(Lang::Js, valueFor(Lang::Js)));
	fireTrigger(sender);
	receiver->processBypass(Test::makeProcessArgs(0));
	REQUIRE(drainLog(receiver).find("got transport") != std::string::npos);

	// And a bypassed sender's broadcasts still go out.
	MidiKitModule* receiver2 = rig.load(Lang::Js, receiverScript(Lang::Js, "'got2'"));
	sender->host.getActiveEngine()->processInTick(0, 0);
	sender->processBypass(Test::makeProcessArgs(1));
	receiver2->host.getActiveEngine()->process();
	REQUIRE(drainLog(receiver2).find("got2") != std::string::npos);
}

TEST_CASE("A module with an injected worker and no bus receives nothing", "[MidiKit][Broadcast]") {
	BroadcastRig rig;
	Kit<> isolatedKit;   // no bus: it gets a private one
	MidiKitModule* isolated = isolatedKit.m;
	isolated->loadScript(receiverScript(Lang::Lua, "'got'"));
	drainLog(isolated);
	MidiKitModule* sender = rig.load(Lang::Lua, senderScript(Lang::Lua, "1"));
	fireTrigger(sender);
	REQUIRE(drainLog(sender).find("sent 0") != std::string::npos);
	isolated->host.getActiveEngine()->process();
	REQUIRE(drainLog(isolated).find("got") == std::string::npos);
}

TEST_CASE("The return value is the number of receivers", "[MidiKit][Broadcast]") {
	BroadcastRig rig;
	for (int i = 0; i < 3; i++) rig.load(Lang::Js, receiverScript(Lang::Js, "'got'"));
	MidiKitModule* sender = rig.load(Lang::Lua, senderScript(Lang::Lua, "1"));
	fireTrigger(sender);
	REQUIRE(drainLog(sender).find("sent 3") != std::string::npos);
}

TEST_CASE("Reloading a script changes membership and drops queued broadcasts", "[MidiKit][Broadcast]") {
	BroadcastRig rig;
	MidiKitModule* receiver = rig.load(Lang::Lua, receiverScript(Lang::Lua, "'old'"));
	MidiKitModule* sender = rig.load(Lang::Js, senderScript(Lang::Js, "1"));

	// Queued for the old script, then the script is replaced before it runs.
	fireTrigger(sender);
	receiver->loadScript(receiverScript(Lang::Js, "'new'"));
	drainLog(receiver);
	receiver->host.getActiveEngine()->process();
	REQUIRE(drainLog(receiver).empty());

	// The new script has the hook, so it receives.
	drainLog(sender);
	fireTrigger(sender);
	REQUIRE(drainLog(sender).find("sent 1") != std::string::npos);
	receiver->host.getActiveEngine()->process();
	REQUIRE(drainLog(receiver).find("new") != std::string::npos);

	// A script without the hook leaves the bus.
	receiver->loadScript(scriptFor(Lang::Lua, "rack.log('plain')\n"));
	drainLog(receiver);
	fireTrigger(sender);
	REQUIRE(drainLog(sender).find("sent 0") != std::string::npos);
	REQUIRE(rig.bus->receivers.empty());
}

TEST_CASE("A Lua memory-limit stop removes the engine from the bus", "[MidiKit][Broadcast]") {
	BroadcastRig rig;
	MidiKitModule* m = rig.create();
	m->loadScript(scriptFor(Lang::Lua, "function rack.onBroadcast(msg) end\nbig = string.rep('x', 2 * 1024 * 1024)\n"));
	REQUIRE(m->host.seLua.L == nullptr);
	REQUIRE(rig.bus->receivers.empty());
}

TEST_CASE("More broadcasts than the queue holds are dropped and logged once", "[MidiKit][Broadcast]") {
	for (Lang lang : {Lang::Lua, Lang::Js}) {
		BroadcastRig rig;
		MidiKitModule* receiver = rig.load(lang, receiverScript(lang, "'got'"));
		std::string flood = lang == Lang::Lua
			? scriptFor(lang, "trig.enableIn(1)\nfunction trig.onTrigger(p)\n  for i = 1, 17 do rack.sendBroadcast(i) end\nend\n")
			: scriptFor(lang, "trig.enableIn(1);\ntrig.onTrigger = function(p) { for (let i = 1; i <= 17; i++) rack.sendBroadcast(i); };\n");
		MidiKitModule* sender = rig.load(lang, flood);
		fireTrigger(sender);

		std::string overflow = drainLog(receiver);
		REQUIRE(countOf(overflow, "Broadcast input queue full") == 1);
		receiver->host.getActiveEngine()->process();
		REQUIRE(countOf(drainLog(receiver), "got") == 16);
	}
}

TEST_CASE("Broadcast size cap: just under is delivered, just over is rejected", "[MidiKit][Broadcast]") {
	for (Lang lang : {Lang::Lua, Lang::Js}) {
		auto valueOfSize = [&](int n) {
			return lang == Lang::Lua ? "{s = string.rep('x', " + std::to_string(n) + ")}"
			                         : "{s: 'x'.repeat(" + std::to_string(n) + ")}";
		};
		BroadcastRig rig;
		MidiKitModule* receiver = rig.load(lang, receiverScript(lang, lang == Lang::Lua ? "'len ' .. number.toString(#msg.s)" : "'len ' + number.toString(msg.s.length)"));

		// {"s":"..."} is 8 bytes plus the string.
		MidiKitModule* ok = rig.load(lang, senderScript(lang, valueOfSize(4000)));
		fireTrigger(ok);
		REQUIRE(drainLog(ok).find("sent 1") != std::string::npos);
		receiver->host.getActiveEngine()->process();
		REQUIRE(drainLog(receiver).find("len 4000") != std::string::npos);

		MidiKitModule* big = rig.load(lang, senderScript(lang, valueOfSize(4100)));
		fireTrigger(big);
		std::string log = drainLog(big);
		REQUIRE(log.find("exceeds the 4 KB limit") != std::string::npos);
		REQUIRE(log.find("sent 0") != std::string::npos);
	}
}

TEST_CASE("Values that cannot be broadcast are rejected without stopping the script", "[MidiKit][Broadcast]") {
	for (Lang lang : {Lang::Lua, Lang::Js}) {
		BroadcastRig rig;
		MidiKitModule* receiver = rig.load(lang, receiverScript(lang, "'got'"));
		(void)receiver;
		const char* values[] = {
			lang == Lang::Lua ? "function() end" : "function() {}",
			lang == Lang::Lua ? "{a = {b = {c = {d = {e = 1}}}}}" : "{a: {b: {c: {d: {e: 1}}}}}",
		};
		for (const char* v : values) {
			MidiKitModule* sender = rig.load(lang, senderScript(lang, v));
			fireTrigger(sender);
			std::string log = drainLog(sender);
			REQUIRE(log.find("not JSON-serializable") != std::string::npos);
			// The script kept running: the line after the call executed.
			REQUIRE(log.find("sent 0") != std::string::npos);
		}

		// Without a value, the call is a script error that the script can catch.
		std::string noArg = lang == Lang::Lua
			? scriptFor(lang, "trig.enableIn(1)\nfunction trig.onTrigger(p)\n  local ok = pcall(rack.sendBroadcast)\n  rack.log('ok ' .. tostring(ok))\nend\n")
			: scriptFor(lang, "trig.enableIn(1);\ntrig.onTrigger = function(p) {\n  let ok = true;\n  try { rack.sendBroadcast(); } catch (e) { ok = false; }\n  rack.log('ok ' + String(ok));\n};\n");
		MidiKitModule* m = rig.load(lang, noArg);
		fireTrigger(m);
		REQUIRE(drainLog(m).find("ok false") != std::string::npos);
	}
}

TEST_CASE("Two scripts that reply to each other are bounded per process", "[MidiKit][Broadcast]") {
	for (Lang lang : {Lang::Lua, Lang::Js}) {
		BroadcastRig rig;
		std::string echo = lang == Lang::Lua
			? scriptFor(lang, "function rack.onBroadcast(msg)\n  rack.log('r')\n  rack.sendBroadcast(msg)\nend\n")
			: scriptFor(lang, "rack.onBroadcast = function(msg) { rack.log('r'); rack.sendBroadcast(msg); };\n");
		MidiKitModule* a = rig.load(lang, echo);
		MidiKitModule* b = rig.load(lang, echo);
		MidiKitModule* kicker = rig.load(lang, senderScript(lang, "1"));
		fireTrigger(kicker);

		const int rounds = 6;
		for (int i = 0; i < rounds; i++) {
			a->host.getActiveEngine()->process();
			b->host.getActiveEngine()->process();
			// A reply is answered on the next pass, so a queue never grows.
			REQUIRE(a->host.getActiveEngine()->broadcastInQueue.size() <= 2);
			REQUIRE(b->host.getActiveEngine()->broadcastInQueue.size() <= 2);
		}
		size_t replies = countOf(drainLog(a), "r") + countOf(drainLog(b), "r");
		REQUIRE(replies <= (size_t)rounds * 2 * 2);
		REQUIRE(replies > 0);
	}
}

TEST_CASE("A broadcast is dispatched on the sender's event frame", "[MidiKit][Broadcast]") {
	auto bus = std::make_shared<StoermelderPackOne::MidiScript::BroadcastBus>();
	struct FrameEngine : BusEngine {
		std::vector<int64_t> frames;
		void dispatchBroadcast(const StoermelderPackOne::MidiScript::InboundBroadcast&) override { frames.push_back(currentInFrame); }
	};
	FrameEngine sender, receiver;
	sender.useBus(bus);
	receiver.useBus(bus);
	bus->join(&receiver);

	sender.currentInFrame = 1234;
	REQUIRE(sender.sendBroadcast(json_integer(1)) == 1);
	sender.currentInFrame = -1;
	REQUIRE(sender.sendBroadcast(json_integer(2)) == 1);
	receiver.process();
	REQUIRE(receiver.frames == std::vector<int64_t>{1234, -1});
	REQUIRE(receiver.currentInFrame == -1);
	bus->leave(&receiver);
}

TEST_CASE("The bus is empty once every module is destroyed", "[MidiKit][Broadcast]") {
	auto bus = std::make_shared<StoermelderPackOne::MidiScript::BroadcastBus>();
	{
		BroadcastRig rig;
		rig.bus = bus;
		rig.load(Lang::Lua, receiverScript(Lang::Lua, "'got'"));
		rig.load(Lang::Js, receiverScript(Lang::Js, "'got'"));
		REQUIRE(bus->receivers.size() == 2);
	}
	REQUIRE(bus->receivers.empty());
}

TEST_CASE("A topic is passed to onBroadcast as a second argument, between all engine pairs", "[MidiKit][Broadcast][Topic]") {
	for (Lang from : {Lang::Lua, Lang::Js}) {
		for (Lang to : {Lang::Lua, Lang::Js}) {
			BroadcastRig rig;
			MidiKitModule* receiver = rig.create();
			receiver->loadScript(to == Lang::Lua
				? scriptFor(to, "function rack.onBroadcast(msg, topic)\n  rack.log('got ' .. tostring(topic) .. ' ' .. number.toString(msg))\nend\n")
				: scriptFor(to, "rack.onBroadcast = function(msg, topic) { rack.log('got ' + String(topic) + ' ' + number.toString(msg)); };\n"));
			drainLog(receiver);

			// A script that sends once with a topic, once with an empty one, once without.
			std::string sends = from == Lang::Lua
				? "rack.sendBroadcast(1, 'clock')\nrack.sendBroadcast(2, '')\nrack.sendBroadcast(3)\n"
				: "rack.sendBroadcast(1, 'clock');\nrack.sendBroadcast(2, '');\nrack.sendBroadcast(3);\n";
			MidiKitModule* sender = rig.create();
			sender->loadScript(scriptFor(from, sends));
			REQUIRE(drainLog(sender).find("rror") == std::string::npos);

			receiver->host.getActiveEngine()->process();
			std::string log = drainLog(receiver);
			REQUIRE(log.find("got clock 1") != std::string::npos);
			REQUIRE(log.find("got  2") != std::string::npos);
			// No topic: nil in Lua, undefined in JS.
			REQUIRE(log.find(to == Lang::Lua ? "got nil 3" : "got undefined 3") != std::string::npos);
		}
	}
}

TEST_CASE("A topic that is not a string or is too long is rejected", "[MidiKit][Broadcast][Topic]") {
	for (Lang lang : {Lang::Lua, Lang::Js}) {
		BroadcastRig rig;
		MidiKitModule* receiver = rig.load(lang, receiverScript(lang, "'got'"));
		std::string longTopic = std::string(65, 'x');
		std::string okTopic = std::string(64, 'x');
		std::string body = lang == Lang::Lua
			? "rack.log('a ' .. number.toString(rack.sendBroadcast(1, 5)))\n"
			  "rack.log('b ' .. number.toString(rack.sendBroadcast(1, '" + longTopic + "')))\n"
			  "rack.log('c ' .. number.toString(rack.sendBroadcast(1, '" + okTopic + "')))\n"
			: "rack.log('a ' + number.toString(rack.sendBroadcast(1, 5)));\n"
			  "rack.log('b ' + number.toString(rack.sendBroadcast(1, '" + longTopic + "')));\n"
			  "rack.log('c ' + number.toString(rack.sendBroadcast(1, '" + okTopic + "')));\n";
		MidiKitModule* sender = rig.create();
		sender->loadScript(scriptFor(lang, body));
		std::string log = drainLog(sender);
		REQUIRE(log.find("topic must be a string") != std::string::npos);
		REQUIRE(log.find("topic exceeds 64 bytes") != std::string::npos);
		REQUIRE(log.find("a 0") != std::string::npos);
		REQUIRE(log.find("b 0") != std::string::npos);
		REQUIRE(log.find("c 1") != std::string::npos);
		receiver->host.getActiveEngine()->process();
		REQUIRE(countOf(drainLog(receiver), "got") == 1);
	}
}
