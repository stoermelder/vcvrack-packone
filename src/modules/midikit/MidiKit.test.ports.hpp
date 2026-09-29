#include "MidiKit.test.hpp"

// Module variants: MidiKitModuleBase/MidiKitWidgetBase instantiated with other
// port counts than the original MidiKit. The rest of the suite only exercises
// the 1-in/1-out config, so multi-port routing and the MidiKitMicro variant are
// covered here.

// 3 CV inputs, 1 param, 2 trigger in/out, 2 MIDI in/out.
struct MultiConfig {
	static constexpr int cvInputs = 3;
	static constexpr int trigInputs = 2;
	static constexpr int trigOutputs = 2;
	static constexpr int params = 1;
	static constexpr int midiInputs = 2;
	static constexpr int midiOutputs = 2;
};
using MultiModule = MidiKitModuleBase<MultiConfig>;

// A widget for MultiConfig without a registered model: two of each MIDI port,
// no controls. Enough to build the base's context menu.
struct MultiWidget : MidiKitWidgetBase<MultiConfig> {
	MultiWidget(MultiModule* module) : MidiKitWidgetBase<MultiConfig>(module, "MidiKit") {
		addMidiInputDisplay(0, Rect(Vec(0.f, 36.4f), Vec(180.f, 44.6f)));
		addMidiInputDisplay(1, Rect(Vec(0.f, 81.f), Vec(180.f, 44.6f)));
		addLogDisplay(Rect(Vec(0.f, 126.f), Vec(180.f, 60.f)));
		addMidiOutputDisplay(0, Rect(Vec(0.f, 190.f), Vec(180.f, 44.6f)));
		addMidiOutputDisplay(1, Rect(Vec(0.f, 235.f), Vec(180.f, 44.6f)));
	}
};

static MultiModule* createMultiModule() {
	MultiModule* m = new MultiModule(std::make_shared<StoermelderPackOne::SyncTaskWorker>());
	m->id = rand();
	Module::SampleRateChangeEvent e{44100.f, 1.f / 44100.f};
	m->onSampleRateChange(e);
	return m;
}

struct MultiScaffold : Test::ModuleScaffold<MultiModule> {
	MultiScaffold() : Test::ModuleScaffold<MultiModule>([]() { return createMultiModule(); }) {}
};

static midi::Message ccMsg(uint8_t ch, uint8_t num, uint8_t value) {
	return Test::makeMidiMessage(0xb, ch, num, value);
}

// Runs enough process() calls to pass the module's process divider once.
static void pump(MultiModule* m, int64_t& frame) {
	for (int i = 0; i < 9; i++) m->process(Test::makeProcessArgs(frame++));
}

static const char* QUICKJS_EMPTY =
	"/**\n"
	" * @engine QuickJs@v1\n"
	" */\n";

static std::string probes(MultiModule* m) {
	std::string all, out;
	std::tuple<LOG_FORMAT, float, std::string> t;
	while (m->log.midiLogMessages.try_pop(t)) all += std::get<2>(t) + "\n";
	return all;
}

TEST_CASE("Variant: sizes follow the config", "[MidiKit][Variant]") {
	MultiScaffold mods;
	MultiModule* m = mods.create();

	REQUIRE(m->NUM_PARAMS == 1);
	REQUIRE(m->NUM_INPUTS == 3 + 2);
	REQUIRE(m->NUM_OUTPUTS == 2);
	REQUIRE(MultiModule::INPUT_TRIG == 3);
	REQUIRE(MultiModule::OUTPUT_TRIG == 0);

	StoermelderPackOne::MidiScript::MidiScriptEngine* se = &m->host.seLua;
	REQUIRE(se->inputCount == 3);
	REQUIRE(se->inputTrigCount == 2);
	REQUIRE(se->outputTrigCount == 2);
	REQUIRE(se->paramCount == 1);
	REQUIRE(se->midiInputCount == 2);
	REQUIRE(se->midiOutputCount == 2);
}

TEST_CASE("Variant: micro config sizes", "[MidiKit][Variant]") {
	Test::ModuleScaffold<MidiKitMicroModule> mods([]() {
		MidiKitMicroModule* m = new MidiKitMicroModule(std::make_shared<StoermelderPackOne::SyncTaskWorker>());
		m->id = rand();
		return m;
	});
	MidiKitMicroModule* m = mods.create();

	REQUIRE(m->NUM_PARAMS == 2);
	REQUIRE(m->NUM_INPUTS == 3);    // 2 CV + 1 trigger
	REQUIRE(m->NUM_OUTPUTS == 1);
	REQUIRE(m->host.seLua.inputCount == 2);
	REQUIRE(m->host.seLua.paramCount == 2);
}

// ── MIDI output routing ─────────────────────────────────────────────────────

// Tick-scheduled messages park in the target output's tickQueue, which makes
// the routing observable without a MIDI driver.
TEST_CASE("Variant: sendMidi routes to the addressed MIDI output", "[MidiKit][Variant]") {
	MultiScaffold mods;
	MultiModule* m = mods.create();
	m->loadScript(QUICKJS_EMPTY);
	int64_t frame = 1;

	midi::Message msg = ccMsg(0, 7, 100);
	REQUIRE(m->sendMidi(1, &msg, 1, 0, 5));
	pump(m, frame);
	REQUIRE(m->midiOutputs[0].tickQueue[0].size() == 0);
	REQUIRE(m->midiOutputs[1].tickQueue[0].size() == 1);

	REQUIRE(m->sendMidi(0, &msg, 1, 0, 5));
	pump(m, frame);
	REQUIRE(m->midiOutputs[0].tickQueue[0].size() == 1);
	REQUIRE(m->midiOutputs[1].tickQueue[0].size() == 1);
}

TEST_CASE("Variant: sendMidi drops an out-of-range MIDI port", "[MidiKit][Variant]") {
	MultiScaffold mods;
	MultiModule* m = mods.create();
	m->loadScript(QUICKJS_EMPTY);

	midi::Message msg = ccMsg(0, 7, 100);
	REQUIRE_FALSE(m->sendMidi(2, &msg, 1, 0, 5));
	REQUIRE_FALSE(m->sendMidi(-1, &msg, 1, 0, 5));
	REQUIRE(m->midiOutQueue.empty());
}

TEST_CASE("Variant: flushOutput sends to the queued port without crashing", "[MidiKit][Variant]") {
	MultiScaffold mods;
	MultiModule* m = mods.create();
	m->loadScript(QUICKJS_EMPTY);

	midi::Message msg = ccMsg(0, 7, 100);
	REQUIRE(m->sendMidi(1, &msg, 1, 0, 0));
	m->flushOutput();
	REQUIRE(m->midiOutQueue.empty());
}

// ── MIDI input routing ──────────────────────────────────────────────────────

static const char* JS_PORT_PROBE = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(midiPort, msg) {
    rack.log("P:" + midiPort);
};
)";

static const char* LUA_PORT_PROBE = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg)
    rack.log("P:" .. midiPort)
end
)";

TEST_CASE("Variant: incoming MIDI reaches the script with its 1-based port", "[MidiKit][Variant]") {
	for (const char* script : {JS_PORT_PROBE, LUA_PORT_PROBE}) {
		MultiScaffold mods;
		MultiModule* m = mods.create();
		m->loadScript(script);
		probes(m);   // drop load-time entries
		int64_t frame = 1;

		m->midiInputs[0].queue.onMessage(ccMsg(0, 1, 10));
		m->midiInputs[1].queue.onMessage(ccMsg(0, 2, 20));
		m->midiInputs[1].queue.onMessage(ccMsg(0, 3, 30));
		pump(m, frame);

		std::string log = probes(m);
		CATCH_INFO(script);
		CATCH_INFO(log);
		size_t p1 = 0, p2 = 0;
		for (size_t pos = log.find("P:1"); pos != std::string::npos; pos = log.find("P:1", pos + 1)) p1++;
		for (size_t pos = log.find("P:2"); pos != std::string::npos; pos = log.find("P:2", pos + 1)) p2++;
		REQUIRE(p1 == 1);
		REQUIRE(p2 == 2);
	}
}

TEST_CASE("Variant: extended-CC enables are per MIDI input", "[MidiKit][Variant]") {
	MultiScaffold mods;
	MultiModule* m = mods.create();
	m->loadScript(QUICKJS_EMPTY);

	m->enableNrpnIn(1, 0, 3);
	REQUIRE(m->isNrpnEnabled(3, false, 1));
	REQUIRE_FALSE(m->isNrpnEnabled(3, false, 0));

	m->enableCc14bitIn(0, 7, 2);
	REQUIRE(m->isCc14bitEnabled(2, 7, 0));
	REQUIRE_FALSE(m->isCc14bitEnabled(2, 7, 1));

	// A port the module doesn't have is ignored.
	m->enableNrpnIn(2, 0, 5);
	m->enableNrpnIn(-1, 0, 5);
	REQUIRE_FALSE(m->isNrpnEnabled(5, false, 0));
	REQUIRE_FALSE(m->isNrpnEnabled(5, false, 1));

	// A reset drops the enables of every port.
	m->onReset();
	REQUIRE_FALSE(m->isNrpnEnabled(3, false, 1));
	REQUIRE_FALSE(m->isCc14bitEnabled(2, 7, 0));
}

// ── Trigger ports ───────────────────────────────────────────────────────────

TEST_CASE("Variant: each trigger input has its own tick clock", "[MidiKit][Variant]") {
	MultiScaffold mods;
	MultiModule* m = mods.create();
	m->loadScript(QUICKJS_EMPTY);
	m->enableTrigger(0, 0);
	m->enableTrigger(1, 0);
	m->inputs[MultiModule::INPUT_TRIG + 0].channels = 1;
	m->inputs[MultiModule::INPUT_TRIG + 1].channels = 1;
	int64_t frame = 0;

	m->process(Test::makeProcessArgs(frame++));   // prime both LOW
	m->inputs[MultiModule::INPUT_TRIG + 1].setVoltage(10.f);
	m->process(Test::makeProcessArgs(frame++));
	REQUIRE(m->triggersIn.triggerTick[0][0] == 0);
	REQUIRE(m->triggersIn.triggerTick[1][0] == 1);
	REQUIRE(m->getTrigTicks(1, 0) == 1);
	REQUIRE(m->getTrigTicks(0, 0) == 0);
}

TEST_CASE("Variant: scheduled MIDI flushes on trigger input 0 only", "[MidiKit][Variant]") {
	MultiScaffold mods;
	MultiModule* m = mods.create();
	m->loadScript(QUICKJS_EMPTY);
	m->enableTrigger(0, 0);
	m->enableTrigger(1, 0);
	m->inputs[MultiModule::INPUT_TRIG + 0].channels = 1;
	m->inputs[MultiModule::INPUT_TRIG + 1].channels = 1;

	// Due at tick 1 on both outputs.
	midi::Message msg = ccMsg(0, 7, 100);
	m->midiOutputs[0].send(msg, 0, 1);
	m->midiOutputs[1].send(msg, 0, 1);
	int64_t frame = 0;
	m->process(Test::makeProcessArgs(frame++));   // prime LOW

	// A rising edge on trigger input 1 counts a tick, but flushes nothing.
	m->inputs[MultiModule::INPUT_TRIG + 1].setVoltage(10.f);
	m->process(Test::makeProcessArgs(frame++));
	REQUIRE(m->triggersIn.triggerTick[1][0] == 1);
	REQUIRE(m->midiOutputs[0].tickQueue[0].size() == 1);
	REQUIRE(m->midiOutputs[1].tickQueue[0].size() == 1);

	// A rising edge on trigger input 0 flushes every MIDI output.
	m->inputs[MultiModule::INPUT_TRIG + 0].setVoltage(10.f);
	m->process(Test::makeProcessArgs(frame++));
	REQUIRE(m->triggersIn.triggerTick[0][0] == 1);
	REQUIRE(m->midiOutputs[0].tickQueue[0].size() == 0);
	REQUIRE(m->midiOutputs[1].tickQueue[0].size() == 0);
}

TEST_CASE("Variant: trigger outputs are addressed by index", "[MidiKit][Variant]") {
	MultiScaffold mods;
	MultiModule* m = mods.create();

	m->setTrigVoltage(1, 0, 4.f);
	REQUIRE(m->outputs[MultiModule::OUTPUT_TRIG + 1].getVoltage(0) == 4.f);
	REQUIRE(m->outputs[MultiModule::OUTPUT_TRIG + 0].getVoltage(0) == 0.f);
}

TEST_CASE("Variant: CV inputs and params are addressed by index", "[MidiKit][Variant]") {
	MultiScaffold mods;
	MultiModule* m = mods.create();

	// Disabled until the script enables them.
	m->inputs[MultiModule::INPUT + 2].setVoltage(3.f);
	m->params[MultiModule::PARAM + 0].setValue(0.5f);
	REQUIRE(m->getInputVoltage(2, 0) == 0.f);
	REQUIRE(m->getParamValue(0) == 0.f);

	m->enableInput(2);
	m->enableParam(0);
	REQUIRE(m->getInputVoltage(2, 0) == 3.f);
	REQUIRE(m->getInputVoltage(1, 0) == 0.f);
	REQUIRE(m->getParamValue(0) == 0.5f);
}

// ── Persistence ─────────────────────────────────────────────────────────────

TEST_CASE("Variant: MIDI port JSON keys keep the first port's legacy key", "[MidiKit][Variant][JSON]") {
	MultiScaffold mods;
	MultiModule* m = mods.create();

	json_t* rootJ = m->dataToJson();
	REQUIRE(json_object_get(rootJ, "midiInput") != nullptr);
	REQUIRE(json_object_get(rootJ, "midiInput2") != nullptr);
	REQUIRE(json_object_get(rootJ, "midiOutput") != nullptr);
	REQUIRE(json_object_get(rootJ, "midiOutput2") != nullptr);
	REQUIRE(json_object_get(rootJ, "midiInput3") == nullptr);

	// Round trip: the second input's channel survives.
	m->midiInputs[1].queue.channel = 5;
	m->midiOutputs[1].channel = 7;
	json_decref(rootJ);
	rootJ = m->dataToJson();

	MultiScaffold mods2;
	MultiModule* m2 = mods2.create();
	m2->dataFromJson(rootJ);
	json_decref(rootJ);
	REQUIRE(m2->midiInputs[1].queue.channel == 5);
	REQUIRE(m2->midiOutputs[1].channel == 7);
	REQUIRE(m2->midiInputs[0].queue.channel == m->midiInputs[0].queue.channel);
}

TEST_CASE("Variant: a single-port patch loads into the first port", "[MidiKit][Variant][JSON]") {
	MultiScaffold mods;
	MultiModule* m = mods.create();

	// The single-port MidiKit saves only "midiInput"/"midiOutput".
	Test::ModuleScaffold<MidiKitModule> single([]() { return createModule(); });
	MidiKitModule* s = single.create();
	s->midiInput.channel = 9;
	json_t* rootJ = s->dataToJson();
	m->dataFromJson(rootJ);
	json_decref(rootJ);

	REQUIRE(m->midiInputs[0].queue.channel == 9);
}

// ── Widgets ─────────────────────────────────────────────────────────────────

static int countMenuEntries(rack::ui::Menu* menu, const std::string& text) {
	int n = 0;
	for (rack::Widget* child : menu->children) {
		if (auto* mi = dynamic_cast<rack::ui::MenuItem*>(child)) {
			if (mi->text == text) n++;
		}
		else if (auto* ml = dynamic_cast<rack::ui::MenuLabel*>(child)) {
			if (ml->text == text) n++;
		}
	}
	return n;
}

// The "Log" submenu of `menu` (owned by the caller), or null without one.
static rack::ui::Menu* createLogSubmenu(rack::ui::Menu* menu) {
	for (rack::Widget* child : menu->children) {
		auto* mi = dynamic_cast<rack::ui::MenuItem*>(child);
		if (mi && mi->text == "Log") return mi->createChildMenu();
	}
	return nullptr;
}

TEST_CASE("Variant: context menu lists every MIDI port", "[MidiKit][Variant][ContextMenu]") {
	MultiScaffold mods;
	MultiModule* m = mods.create();
	MultiWidget* mw = new MultiWidget(m);

	rack::ui::Menu* menu = new rack::ui::Menu;
	mw->appendContextMenu(menu);
	REQUIRE(countMenuEntries(menu, "MIDI input 1") == 1);
	REQUIRE(countMenuEntries(menu, "MIDI input 2") == 1);
	REQUIRE(countMenuEntries(menu, "MIDI output 1") == 1);
	REQUIRE(countMenuEntries(menu, "MIDI output 2") == 1);
	REQUIRE(countMenuEntries(menu, "MIDI input") == 0);

	delete menu;
	Test::destroyWidget(mw);
}

TEST_CASE("Variant: single-port MidiKit context menu has unnumbered MIDI items", "[MidiKit][Variant][ContextMenu]") {
	ModuleScaffold mods;
	MidiKitModule* m = mods.create();
	m->model = modelMidiKit;
	MidiKitWidget* mw = Test::createWidget<MidiKitWidget>(m);

	rack::ui::Menu* menu = new rack::ui::Menu;
	mw->appendContextMenu(menu);
	REQUIRE(countMenuEntries(menu, "MIDI input") == 1);
	REQUIRE(countMenuEntries(menu, "MIDI output") == 1);
	REQUIRE(countMenuEntries(menu, "Log") == 0);   // only MidiKitMicro has it

	delete menu;
	Test::destroyWidget(mw);
}

TEST_CASE("Variant: MidiKitMicro widget works without a log display", "[MidiKit][Variant][Micro]") {
	Test::ModuleScaffold<MidiKitMicroModule> mods([]() {
		MidiKitMicroModule* m = new MidiKitMicroModule(std::make_shared<StoermelderPackOne::SyncTaskWorker>());
		m->id = rand();
		return m;
	});
	MidiKitMicroModule* m = mods.create();
	m->model = modelMidiKitMicro;
	MidiKitMicroWidget* mw = Test::createWidget<MidiKitMicroWidget>(m);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->logDisplay == nullptr);

	// The log is drained into the widget, which keeps only the newest five
	// lines for the context menu; stepping must not touch the missing display.
	m->loadScript(QUICKJS_EMPTY);   // pushes a RESET, clearing older entries
	for (int i = 0; i < 8; i++) m->writeLog("line" + std::to_string(i), false);
	mw->step();
	std::tuple<LOG_FORMAT, float, std::string> t;
	REQUIRE_FALSE(m->log.midiLogMessages.try_pop(t));
	REQUIRE(mw->buffer.size() == 5);

	// The last lines are in the "Log" submenu, newest first.
	rack::ui::Menu* logMenu = new rack::ui::Menu;
	mw->appendContextMenu(logMenu);
	rack::ui::Menu* sub = createLogSubmenu(logMenu);
	REQUIRE(sub != nullptr);
	REQUIRE(sub->children.size() == 5);
	delete sub;
	delete logMenu;

	// Long lines wrap inside a fixed width: same width, but taller than a
	// short line. The default UiAccess measures text without a window.
	StoermelderPackOne::vcv::UiAccess measureFallback;
	StoermelderPackOne::vcv::uiAccess = &measureFallback;
	std::string longText;
	for (int i = 0; i < 60; i++) longText += "word ";
	m->writeLog(longText, false);
	mw->step();
	rack::ui::Menu* longMenu = new rack::ui::Menu;
	mw->appendContextMenu(longMenu);
	sub = createLogSubmenu(longMenu);
	REQUIRE(sub != nullptr);
	REQUIRE(sub->children.size() == 5);
	auto it = sub->children.begin();
	rack::Widget* longLine = *it++;
	rack::Widget* shortLine = *it;
	longLine->step();
	shortLine->step();
	REQUIRE(longLine->box.size.x == shortLine->box.size.x);
	REQUIRE(longLine->box.size.y > shortLine->box.size.y);   // newest is the 400-char line
	StoermelderPackOne::vcv::uiAccess = nullptr;
	delete sub;
	delete longMenu;

	// A reset clears the menu log.
	m->loadScript(QUICKJS_EMPTY);
	mw->step();
	REQUIRE(mw->buffer.size() < 5);
	mw->resetLog();
	REQUIRE(mw->buffer.empty());
	rack::ui::Menu* emptyMenu = new rack::ui::Menu;
	mw->appendContextMenu(emptyMenu);
	sub = createLogSubmenu(emptyMenu);
	REQUIRE(sub != nullptr);
	REQUIRE(sub->children.size() == 1);   // the "(empty)" placeholder
	delete sub;
	delete emptyMenu;

	// It still has the MIDI menu and the base's script menu.
	rack::ui::Menu* menu = new rack::ui::Menu;
	mw->appendContextMenu(menu);
	REQUIRE(countMenuEntries(menu, "MIDI input") == 1);
	REQUIRE(countMenuEntries(menu, "MIDI output") == 1);
	REQUIRE(countMenuEntries(menu, "Log") == 1);

	delete menu;
	Test::destroyWidget(mw);
}

TEST_CASE("Variant: MidiKitMicro widget exposes its ports", "[MidiKit][Variant][Micro]") {
	Test::ModuleScaffold<MidiKitMicroModule> mods([]() {
		MidiKitMicroModule* m = new MidiKitMicroModule(std::make_shared<StoermelderPackOne::SyncTaskWorker>());
		m->id = rand();
		return m;
	});
	MidiKitMicroModule* m = mods.create();
	m->model = modelMidiKitMicro;
	MidiKitMicroWidget* mw = Test::createWidget<MidiKitMicroWidget>(m);

	REQUIRE(mw->getParams().size() == 2);
	REQUIRE(mw->getInputs().size() == 3);
	REQUIRE(mw->getOutputs().size() == 1);

	Test::destroyWidget(mw);
}
