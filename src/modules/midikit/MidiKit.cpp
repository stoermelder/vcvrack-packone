#include "MidiScriptEngine.h"
#include "MidiScriptEngineLua.h"
#include "MidiScriptEngineQuickJs.h"
#include "../../components/Knobs.hpp"
#include "../../components/MidiWidget.hpp"
#include "../../components/LedTextField.hpp"
#include "../../ui/OverlayMessageWidget.hpp"
#include "../../utils/MpmcTaskWorker.hpp"
#include <osdialog.h>
#include <fstream>
#include <queue>
#include <atomic>

namespace StoermelderPackOne {
namespace MidiKit {


enum class LOG_FORMAT {
	RESET,
	TIMESTAMP,
	INDENTED,
	TEXT
};

struct MidiOutput : midi::Output {
	struct FrameSchedule {
		midi::Message msg;
		bool operator<(const FrameSchedule& other) const {
			return msg.frame > other.msg.frame;
		}
	};
	
	struct TickSchedule {
		midi::Message msg;
		uint64_t tick;
		bool operator<(const TickSchedule& other) const {
			return tick > other.tick;
		}
	};

	std::priority_queue<FrameSchedule> frameQueue;
	std::priority_queue<TickSchedule> tickQueue;

	std::vector<int> getChannels() override {
		std::vector<int> channels;
		for (int c = -1; c < 16; c++) {
			channels.push_back(c);
		}
		return channels;
	}

	void reset() {
		Output::reset();
		while (!frameQueue.empty()) frameQueue.pop();
		while (!tickQueue.empty()) tickQueue.pop();
		channel = -1;
	}

	void send(midi::Message& msg, uint64_t tick) {
		if (tick != 0) {
			TickSchedule s;
			s.msg = msg;
			s.tick = tick;
			tickQueue.push(s);
			return;
		}

		if (msg.frame != -1) {
			FrameSchedule s;
			s.msg = msg;
			frameQueue.push(s);
			return;
		}

		sendMessage(msg);
	}

	void processFrame(int64_t frame) {
		while (true) {
			if (frameQueue.size() == 0) return;
			FrameSchedule s = frameQueue.top();
			// ">=" and not ">": s.msg.frame is the engine frame the message is
			// intended to be processed at (midi.hpp). With ">" a message due
			// exactly at the current frame is deferred to the next processFrame()
			// call — one divider period later. Mirrors the processTick() fix.
			if (frame >= s.msg.frame) {
				frameQueue.pop();
				s.msg.frame = -1;
				sendMessage(s.msg);
			}
			else {
				return;
			}
		}
	}

	void processTick(uint64_t tick) {
		while (true) {
			if (tickQueue.size() == 0) return;
			TickSchedule s = tickQueue.top();
			// ">=" and not "==": process() calls processTick() before draining the
			// engine's out-queue, so a script can schedule for a tick the counter has
			// already consumed. With "==" such a message is never sent and, since the
			// queue is ordered smallest-tick-first, it blocks every later one behind it.
			if (tick >= s.tick) {
				tickQueue.pop();
				sendMessage(s.msg);
			}
			else {
				return;
			}
		}
	}
};


// Returns the one shared async worker for all MidiKit modules.
// The weak_ptr lets it be destroyed when the last module is removed.
//
// No mutex guards the expired()/make_shared/assignment sequence below. This is
// safe because modules are constructed on the UI thread, which is a single
// thread, so defaultWorker() is never called concurrently -- the static
// weak_ptr is only ever touched from that one thread.
static std::shared_ptr<ITaskWorker> defaultWorker() {
	static std::weak_ptr<ITaskWorker> shared;
	if (shared.expired()) {
		auto worker = std::make_shared<MpmcTaskWorker>("MidiKit worker");
		shared = worker;
		return worker;
	}
	return shared.lock();
}

struct MidiKitModule : Module, MidiScript::MidiScriptEngineHandler {
	enum ParamIds {
		ENUMS(PARAM, 4),
		NUM_PARAMS
	};
	enum InputIds {
		ENUMS(INPUT, 4),
		INPUT_TRIG,
		NUM_INPUTS
	};
	enum OutputIds {
		OUTPUT_TRIG,
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	/** [Stored to JSON] */
	int panelTheme = 0;

	/** [Stored to Json] */
	midi::InputQueue midiInput;
	/** [Stored to Json] */
	MidiOutput midiOutput;
	/** [Stored to Json] */
	std::string script = "";
	/** [Stored to Json] */
	std::string scriptConfigJson = "";

	// MPMC queue: midiLogMessages is pushed from the worker thread (writeLog)
	// and from the caller of loadScript/onReset, so it needs concurrent-producer
	// support rather than dsp::RingBuffer's single-producer contract.
	rigtorp::MPMCQueue<std::tuple<LOG_FORMAT, float, std::string>> midiLogMessages{512};

	dsp::RingBuffer<int, 8> overlayQueue;
	std::tuple<std::string, std::string, std::string> overlayMessage;

	dsp::ClockDivider processDivider;
	dsp::Timer rateLimiterTimer;

	dsp::SchmittTrigger inputTrigger;
	uint64_t inputTriggerTick;
	bool outputTriggerActive[PORT_MAX_CHANNELS];
	dsp::PulseGenerator outputPulseGenerator[PORT_MAX_CHANNELS];

	uint64_t sample;
	float sampleRate;

	// ── MidiScriptEngineHandler ──────────────────────────────────────────────
	// The engines call back into the module through these methods for every
	// module-facing operation (log/overlay/input/trig/param).

	// MidiScriptEngineHandler
	void writeLog(const std::string& log, bool useTimestamp = true) override {
		float timestamp = sampleRate != 0.f ? float(sample) / sampleRate : 0.f;
		if (useTimestamp) {
			midiLogMessages.try_push(std::make_tuple(LOG_FORMAT::TIMESTAMP, timestamp, log));
		}
		else {
			midiLogMessages.try_push(std::make_tuple(LOG_FORMAT::TEXT, timestamp, log));
		}
	}

	// MidiScriptEngineHandler
	void writeOverlay(const std::string& s1, const std::string& s2, const std::string& s3) override {
		overlayQueue.push(0);
		overlayMessage = std::make_tuple(s1, s2, s3);
	}

	// MidiScriptEngineHandler
	void enableInput(int i) override {
		reinterpret_cast<MidiScript::MidiScriptEnginePortInfo*>(inputInfos[i])->enabled = true;
	}

	// MidiScriptEngineHandler
	float getInputVoltage(int i, uint8_t ch) override {
		if (reinterpret_cast<MidiScript::MidiScriptEnginePortInfo*>(inputInfos[i])->enabled)
			return inputs[INPUT + i].getVoltage(ch);
		return 0.f;
	}

	// MidiScriptEngineHandler
	float getTrigVoltage(int i, uint8_t ch) override {
		return inputs[INPUT_TRIG + i].getVoltage(ch);
	}

	// MidiScriptEngineHandler
	uint64_t getTrigTicks(int i) override {
		return inputTriggerTick;
	}

	// MidiScriptEngineHandler
	void enableParam(int i) override {
		reinterpret_cast<MidiScript::MidiScriptEngineParamQuantity*>(paramQuantities[i])->enabled = true;
	}

	// MidiScriptEngineHandler
	float getParamValue(int i) override {
		if (reinterpret_cast<MidiScript::MidiScriptEngineParamQuantity*>(paramQuantities[i])->enabled)
			return params[PARAM + i].getValue();
		return 0.f;
	}

	// MidiScriptEngineHandler
	void setTrig(int i, uint8_t ch, float duration = 1e-3f) override {
		outputTriggerActive[ch] = true;
		outputPulseGenerator[ch].trigger(duration);
	}

	// MidiScriptEngineHandler
	void setTrigVoltage(int i, uint8_t ch, float voltage) override {
		outputTriggerActive[ch] = false;
		outputs[OUTPUT_TRIG].setVoltage(voltage, ch);
	}

	// Port/param counts injected into both engines at construction.
	static constexpr int engineInputCount = 4;
	static constexpr int engineInputTrigCount = 1;
	static constexpr int engineOutputTrigCount = 1;
	static constexpr int engineParamCount = 4;
	static constexpr int engineMidiInputCount = 1;
	static constexpr int engineMidiOutputCount = 1;

	MidiScript::Lua::MidiScriptEngineLua seLua;
	MidiScript::QuickJs::MidiScriptEngineQuickJs seQuickJs;
	MidiScript::MidiScriptEngine* activeEngine = nullptr;

	MidiKitModule() : MidiKitModule(defaultWorker()) {}
	explicit MidiKitModule(std::shared_ptr<ITaskWorker> worker)
		: seLua(this, engineInputCount, engineInputTrigCount, engineOutputTrigCount, engineParamCount, engineMidiInputCount, engineMidiOutputCount),
		  seQuickJs(this, engineInputCount, engineInputTrigCount, engineOutputTrigCount, engineParamCount, engineMidiInputCount, engineMidiOutputCount) {
		panelTheme = pluginSettings.panelThemeDefault;
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configInput(INPUT_TRIG, "Trigger");
		configOutput(OUTPUT_TRIG, "Trigger");
		for (int i = 0; i < 4; i++) {
			configInput<MidiScript::MidiScriptEnginePortInfo>(INPUT + i)->se = &seQuickJs;
			configParam<MidiScript::MidiScriptEngineParamQuantity>(PARAM + i, 0.f, 1.f, 0.f)->se = &seQuickJs;
		}

		processDivider.setDivision(8);
		seLua.setWorker(worker);
		seQuickJs.setWorker(worker);
		onReset();
	}

	void onReset() override {
		midiInput.reset();
		midiOutput.reset();
		sample = 0;
		inputTriggerTick = 0;
		for (int i = 0; i < 4; i++) {
			reinterpret_cast<MidiScript::MidiScriptEnginePortInfo*>(inputInfos[i])->enabled = false;
			reinterpret_cast<MidiScript::MidiScriptEngineParamQuantity*>(paramQuantities[i])->enabled = false;
		}
		for (uint8_t i = 0; i < PORT_MAX_CHANNELS; i++) {
			outputTriggerActive[i] = true;
			outputPulseGenerator[i].reset();
		}
		activeEngine = nullptr;
		seLua.loadScript("");
		seQuickJs.loadScript("");

		midiLogMessages.try_push(std::make_tuple(LOG_FORMAT::RESET, 0.f, std::string("")));
		midiLogMessages.try_push(std::make_tuple(LOG_FORMAT::TEXT, 0.f, std::string("No script")));
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		sampleRate = e.sampleRate;
	}

	void onRemove(const RemoveEvent& e) override {
		// Runs the active script's onUnload() (all-notes-off etc.) while this
		// module — the engines' handler — is still fully alive. onUnload() calls
		// back into the module via handler->writeLog()/input.*/trig.*/param.*, so
		// it must run before this object (and its members) start tearing down.
		// Calling closeState() later, from each engine's own destructor, would
		// route those callbacks through a dangling handler — undefined behaviour.
		if (activeEngine) {
			activeEngine->closeState();
		}
	}

	void onSave(const SaveEvent& e) override {
		// Ask the active script for its current config (via rack.onUnload())
		// so the latest context-menu setting survives a save/reload cycle.
		// Runs synchronously on the GUI thread, matching the existing
		// loadScript() pattern; onUnload()'s messages are discarded, so saving
		// has no audible side effects.
		if (activeEngine) {
			scriptConfigJson = activeEngine->captureConfig();
		}
	}

	void processBypass(const ProcessArgs& args) override {
		midi::Message msg;
		while (midiInput.tryPop(&msg, args.frame)) {
			(void)0;
		}
		Module::processBypass(args);
	}

	void process(const ProcessArgs& args) override {
		/*
		// play it safe and limit MIDI output rate to 200 Hz.
		const float rateLimiterPeriod = 1 / 200.f;
		bool rateLimiterTriggered = (rateLimiterTimer.process(args.sampleTime) >= rateLimiterPeriod);
		if (rateLimiterTriggered)
			rateLimiterTimer.time -= rateLimiterPeriod;
		else
			return;
		*/

		if (!activeEngine) return;

		if (inputTrigger.process(inputs[INPUT_TRIG].getVoltage())) {
			inputTriggerTick++;
			midiOutput.processTick(inputTriggerTick);
			activeEngine->processInTick(0);
		}

		if (processDivider.process()) {
			midi::Message msg;
			while (midiInput.tryPop(&msg, args.frame)) {
				activeEngine->processInMessage(0, msg);
			}

			activeEngine->process();

			int midiPort;
			int ticks;
			while (activeEngine->processOutMessage(midiPort, msg, ticks)) {
				midiOutput.send(msg, ticks);
			}
			
			midiOutput.processFrame(args.frame);
		}

		for (uint8_t i = 0; i < PORT_MAX_CHANNELS; i++) {
			bool s = outputPulseGenerator[i].process(args.sampleTime);
			if (outputTriggerActive[i]) {
				outputs[OUTPUT_TRIG].setVoltage(s ? 10.f : 0.f, i);
			}
		}

		sample++;
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));

		json_object_set_new(rootJ, "midiInput", midiInput.toJson());
		json_object_set_new(rootJ, "midiOutput", midiOutput.toJson());
		json_object_set_new(rootJ, "script", json_string(script.c_str()));

		if (!scriptConfigJson.empty()) {
			json_t* configJ = json_loads(scriptConfigJson.c_str(), 0, NULL);
			if (configJ) {
				json_object_set_new(rootJ, "scriptConfig", configJ);
			}
		}
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		json_t* panelThemeJ = json_object_get(rootJ, "panelTheme");
		if (panelThemeJ) panelTheme = json_integer_value(panelThemeJ);

		json_t* midiInputJ = json_object_get(rootJ, "midiInput");
		if (midiInputJ && json_is_object(midiInputJ)) midiInput.fromJson(midiInputJ);
		json_t* midiOutputJ = json_object_get(rootJ, "midiOutput");
		if (midiOutputJ && json_is_object(midiOutputJ)) midiOutput.fromJson(midiOutputJ);

		json_t* scriptJ = json_object_get(rootJ, "script");
		if (scriptJ && json_is_string(scriptJ)) {
			// Restore any persisted script config alongside the script itself.
			json_t* configJ = json_object_get(rootJ, "scriptConfig");
			std::string configJson;
			if (configJ && json_is_object(configJ)) {
				char* s = json_dumps(configJ, JSON_COMPACT);
				if (s) {
					configJson = s;
					free(s);
				}
			}
			loadScript(json_string_value(scriptJ), configJson);
		}
	}

	void loadScript(std::string s, std::string configJson = "") {
		script = s;
		sample = 0;
		inputTriggerTick = 0;
		for (int i = 0; i < 4; i++) {
			reinterpret_cast<MidiScript::MidiScriptEnginePortInfo*>(inputInfos[i])->enabled = false;
			reinterpret_cast<MidiScript::MidiScriptEngineParamQuantity*>(paramQuantities[i])->enabled = false;
		}
		midiLogMessages.try_push(std::make_tuple(LOG_FORMAT::RESET, 0.f, std::string("")));

		// Detect engine from script header
		bool isLua = s.find("@engine Lua") != std::string::npos;
		bool isQuickJs = s.find("@engine QuickJs") != std::string::npos;

		MidiScript::MidiScriptEngine* prevEngine = activeEngine;
		activeEngine = nullptr;
		if (isLua) activeEngine = &seLua;
		if (isQuickJs) activeEngine = &seQuickJs;

		// Clear the engine that is no longer active (silently — RESET was already pushed)
		if (prevEngine && prevEngine != activeEngine) {
			prevEngine->loadScript("");
		}

		// Keep port/param info pointers in sync with the active engine
		for (int i = 0; i < 4; i++) {
			reinterpret_cast<MidiScript::MidiScriptEnginePortInfo*>(inputInfos[i])->se = activeEngine;
			reinterpret_cast<MidiScript::MidiScriptEngineParamQuantity*>(paramQuantities[i])->se = activeEngine;
		}

		scriptConfigJson = configJson;
		if (activeEngine) activeEngine->loadScript(script.c_str(), scriptConfigJson);
	}

	void clearScript() {
		loadScript("");
	}
};


struct LogDisplay : LedTextDisplay {
	std::list<std::tuple<LOG_FORMAT, float, std::string>>* buffer;
	bool dirty = true;

	LogDisplay() {
		color = nvgRGB(0xf0, 0xf0, 0xf0);
		bgColor.a = 0.f;
		fontSize = 9.2f;
		textOffset.y += 2.f;
	}

	void step() override {
		LedTextDisplay::step();
		if (dirty) {
			text = "";
			size_t size = std::min(buffer->size(), static_cast<size_t>(box.size.x / fontSize) + 1);
			size_t i = 0;
			for (std::tuple<LOG_FORMAT, float, std::string> s : *buffer) {
				if (i >= size) break;
				LOG_FORMAT f = std::get<0>(s);
				float timestamp = std::get<1>(s);
				switch (f) {
					case LOG_FORMAT::TIMESTAMP:
						text += string::f("[%9.4f] %s\n", timestamp, std::get<2>(s).c_str());
						break;
					case LOG_FORMAT::TEXT:
						text += string::f("%s\n", std::get<2>(s).c_str());
						break;
					case LOG_FORMAT::INDENTED:
						text += string::f("     %s\n", std::get<2>(s).c_str());
						break;
					default:
						break;
				};
			}
		}
	}

	void reset() {
		buffer->clear();
		dirty = true;
	}
};

// Placeholder menu entry that builds the script-registered items
// (rack.registerContextMenu) asynchronously. getContextMenus() evaluates each
// item's onGetValue callback on the worker thread and then invokes its
// callback with the evaluated specs.
struct ScriptContextMenuItems : ui::MenuEntry {
	struct Context {
		std::vector<MidiScript::ContextMenuSpec> specs;
		std::atomic<bool> loaded{false};
	};
	MidiKitModule* module;
	std::shared_ptr<Context> ctx;
	bool built = false;

	ScriptContextMenuItems(MidiKitModule* module) : module(module) {
		ctx = std::make_shared<Context>();
		// Capture a local copy: Apple's Clang rejects capturing the data
		// member `ctx` by name in a capture list.
		std::shared_ptr<Context> c = ctx;
		module->activeEngine->getContextMenus([c](const std::vector<MidiScript::ContextMenuSpec>& specs) {
			// Runs on the worker thread once every onGetValue has been
			// evaluated. Only publishes the specs; the menu widgets are
			// constructed by step() on the UI thread.
			c->specs = specs;
			c->loaded.store(true, std::memory_order_release);
		});
	}

	void step() override {
		if (!built && ctx->loaded.load(std::memory_order_acquire)) {
			built = true;
			buildItems();
			requestDelete();
		}
		ui::MenuEntry::step();
	}

	void buildItems() {
		Menu* menu = dynamic_cast<Menu*>(parent);
		if (!menu) return;
		MidiKitModule* m = module;
		Widget* anchor = this;
		for (const MidiScript::ContextMenuSpec& spec : ctx->specs) {
			Widget* item;
			if (spec.type == MidiScript::ContextMenuSpec::Type::Boolean) {
				item = createMenuItem(spec.label, CHECKMARK(spec.checked), [m, spec]() {
					m->activeEngine->invokeContextMenuCallback(spec.callbackId, spec.checked ? 0 : 1);
				});
			}
			else {
				item = createSubmenuItem(spec.label, "", [m, spec](Menu* sub) {
					for (size_t i = 0; i < spec.options.size(); i++) {
						sub->addChild(createMenuItem(spec.options[i], CHECKMARK(i == static_cast<size_t>(spec.selected)), [m, spec, i]() {
							m->activeEngine->invokeContextMenuCallback(spec.callbackId, static_cast<int>(i));
						}));
					}
				});
			}
			menu->addChildAbove(item, anchor);
			anchor = item;
		}
	}
};

struct MidiKitWidget : ThemedModuleWidget<MidiKitModule>, OverlayMessageProvider {
	const size_t BUFFERSIZE = 800;
	LogDisplay* logDisplay;
	std::list<std::tuple<LOG_FORMAT, float, std::string>> buffer;
	std::string filename = "";

	MidiKitWidget(MidiKitModule *module)
		: ThemedModuleWidget<MidiKitModule>(module, "MidiKit") {
		setModule(module);
		this->module = module;

		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		MidiWidget<>* display1 = createWidget<MidiWidget<>>(Vec(0.f, 36.4f));
		display1->box.size = Vec(180.0f, 44.6f);
		display1->setMidiPort(module ? &module->midiInput : NULL, "In");
		addChild(display1);

		LedDisplay* textDisplay = createWidget<LedDisplay>(Vec(0.f, 81.0f));
		textDisplay->box.size = Vec(180.f, 140.6f);
		addChild(textDisplay);

		logDisplay = createWidget<LogDisplay>(Vec());
		logDisplay->buffer = &buffer;
		logDisplay->box.size = textDisplay->box.size.minus(Vec(0.f, 6.f));
		logDisplay->fontSize = 7.2f;
		textDisplay->addChild(logDisplay);

		MidiWidget<>* display2 = createWidget<MidiWidget<>>(Vec(0.f, 221.6f));
		display2->box.size = Vec(180.0f, 44.6f);
		display2->setMidiPort(module ? &module->midiOutput : NULL, "Out");
		addChild(display2);

		addParam(createParamCentered<StoermelderTrimpot>(Vec(24.7f, 287.3f), module, MidiKitModule::PARAM + 0));
		addParam(createParamCentered<StoermelderTrimpot>(Vec(56.2f, 287.3f), module, MidiKitModule::PARAM + 1));
		addParam(createParamCentered<StoermelderTrimpot>(Vec(87.6f, 287.3f), module, MidiKitModule::PARAM + 2));
		addParam(createParamCentered<StoermelderTrimpot>(Vec(119.1f, 287.3f), module, MidiKitModule::PARAM + 3));

		addInput(createInputCentered<StoermelderPort>(Vec(24.7f, 328.4f), module, MidiKitModule::INPUT + 0));
		addInput(createInputCentered<StoermelderPort>(Vec(56.2f, 328.4f), module, MidiKitModule::INPUT + 1));
		addInput(createInputCentered<StoermelderPort>(Vec(87.6f, 328.4f), module, MidiKitModule::INPUT + 2));
		addInput(createInputCentered<StoermelderPort>(Vec(119.1f, 328.4f), module, MidiKitModule::INPUT + 3));

		addOutput(createOutputCentered<StoermelderPort>(Vec(156.f, 287.3f), module, MidiKitModule::OUTPUT_TRIG));

		addInput(createInputCentered<StoermelderPort>(Vec(156.f, 328.4f), module, MidiKitModule::INPUT_TRIG));

		if (module) {
			OverlayMessageWidget::registerProvider(this);
		}
	}

	~MidiKitWidget() {
		if (module) {
			OverlayMessageWidget::unregisterProvider(this);
		}
	}

	void step() override {
		ThemedModuleWidget<MidiKitModule>::step();
		if (!module) return;
		std::tuple<LOG_FORMAT, float, std::string> s;
		while (module->midiLogMessages.try_pop(s)) {
			if (buffer.size() == BUFFERSIZE) buffer.pop_back();
			if (std::get<0>(s) == LOG_FORMAT::RESET) {
				resetLog();
			}
			else {
				buffer.push_front(s);
				logDisplay->dirty = true;
			}
		}
	}

	void resetLog() {
		buffer.clear();
		logDisplay->reset();
	}

	void appendContextMenu(Menu* menu) override {
		ThemedModuleWidget<MidiKitModule>::appendContextMenu(menu);

		if (module->activeEngine) {
			menu->addChild(new MenuSeparator());
			if (module->activeEngine == &module->seLua) {
				menu->addChild(createMenuLabel("Running Script (Lua)"));
				size_t used;
				if (module->seLua.getMemoryUsage(used)) {
					menu->addChild(createMenuLabel(string::f("RAM usage: %zu KB", used / 1024)));
				}
			}
			if (module->activeEngine == &module->seQuickJs) {
				menu->addChild(createMenuLabel("Running Script (QuickJs)"));
				size_t used, total;
				if (module->seQuickJs.getMemoryUsage(used, total)) {
					float pct = total > 0 ? 100.f * used / total : 0.f;
					menu->addChild(createMenuLabel(string::f("RAM usage: %zu / %zu KB (%.0f%%)", used / 1024, total / 1024, pct)));
				}
			}

			menu->addChild(new ScriptContextMenuItems(module));
		}

		menu->addChild(new MenuSeparator());
		menu->addChild(createMenuLabel("Script"));
		menu->addChild(createSubmenuItem("Examples", "", [=](Menu* menu) {
			menu->addChild(createSubmenuItem("JavaScript", "", [=](Menu* menu) {
				appendExampleItems(menu, asset::plugin(pluginInstance, "presets/MidiKit/JavaScript"), ".js");
			}));
			menu->addChild(createSubmenuItem("Lua", "", [=](Menu* menu) {
				appendExampleItems(menu, asset::plugin(pluginInstance, "presets/MidiKit/Lua"), ".lua");
			}));
		}));
		menu->addChild(createMenuItem("Clear", "", [=]() { module->clearScript(); }));
		menu->addChild(createMenuItem("Paste from clipboard", RACK_MOD_ALT_NAME "+V", [=]() { pasteJsClipboard(); }));
		menu->addChild(createMenuItem("Copy to clipboard", RACK_MOD_ALT_NAME "+C", [=]() { copyJsClipboard(); }));
		menu->addChild(createMenuItem("Load", RACK_MOD_ALT_NAME "+L", [=]() { loadJsDialog(); }));
		menu->addChild(createMenuItem("Reload", RACK_MOD_ALT_NAME "+Y", [=]() { loadJs(filename); }, filename.empty()));
		menu->addChild(createMenuItem("Save as", "", [=]() { saveScriptDialog(); }));
	}

	int nextOverlayMessageId() override {
		if (module->overlayQueue.empty())
			return -1;
		return module->overlayQueue.shift();
	}

	void getOverlayMessage(int id, OverlayMessageProvider::Message& m) override {
		m.title = std::get<0>(module->overlayMessage);
		m.subtitle[0] = std::get<1>(module->overlayMessage);
		m.subtitle[1] = std::get<2>(module->overlayMessage);
	}

	void loadJsDialog() {
		osdialog_filters* filters = osdialog_filters_parse("MIDI-KIT file:js,lua");
		DEFER({
			osdialog_filters_free(filters);
		});

		char* path = osdialog_file(OSDIALOG_OPEN, "", NULL, filters);
		if (!path) {
			// No path selected
			return;
		}
		DEFER({
			free(path);
		});

		filename = path;
		loadJs(path);
	}

	void loadJs(std::string filename) {
		resetLog();

		// Read file
		std::ifstream file;
		file.exceptions(std::ifstream::failbit | std::ifstream::badbit);
		try {
			file.open(filename);
			std::stringstream buffer;
			buffer << file.rdbuf();
			std::string script = buffer.str();
			module->loadScript(script);
		}
		catch (const std::runtime_error& err) {
			// Fail silently
		}
	}

	// Lists .js/.lua example scripts bundled under src/modules/midikit/, sorted, as clickable
	// menu items (mirrors ModuleWidget's factory-preset submenu, but for raw scripts). All
	// other file types in that folder (.cpp, .h, .md, ...) are ignored.
	void appendExampleItems(Menu* menu, std::string dir, std::string ext) {
		bool hasExamples = false;
		if (system::isDirectory(dir)) {
			std::vector<std::string> entries = system::getEntries(dir);
			std::sort(entries.begin(), entries.end());
			for (std::string path : entries) {
				if (system::getExtension(path) != ext) continue;
				hasExamples = true;
				std::string name = system::getStem(path);
				menu->addChild(createMenuItem(name, "", [=]() {
					filename = path;
					loadJs(path);
				}));
			}
		}
		if (!hasExamples) {
			menu->addChild(createMenuLabel("None found"));
		}
	}

	void saveScriptDialog() {
		if (module->script == "")
			return;

		std::string dir = asset::userDir;
		std::string filename = "script.js";
		char* newPathC = osdialog_file(OSDIALOG_SAVE, dir.c_str(), filename.c_str(), NULL);
		if (!newPathC) {
			return;
		}
		std::string newPath = newPathC;
		std::free(newPathC);
		// Add extension if user didn't specify one
		std::string newExt = system::getExtension(system::getFilename(newPath));
		if (newExt == "") newPath += ".js";

		// Write and close file
		{
			std::ofstream f(newPath);
			f << module->script;
		}
	}

	void onPathDrop(const event::PathDrop& e) override {
		if (module && e.paths.size() > 0) {
			loadJs(e.paths[0]);
			e.consume(this);
		}
		ThemedModuleWidget<MidiKitModule>::onPathDrop(e);
	}

	void onHoverKey(const event::HoverKey& e) override {
		if (e.action == GLFW_PRESS && (e.mods & RACK_MOD_MASK) == GLFW_MOD_ALT) {
			if (e.keyName == "c") {
				copyJsClipboard();
				e.consume(this);
			}
			if (e.keyName == "v") {
				pasteJsClipboard();
				e.consume(this);
			}
			if (e.keyName == "l") {
				loadJsDialog();
				e.consume(this);
			}
			if (e.keyName == "y") {
				if (!filename.empty()) {
					loadJs(filename);
				}
				e.consume(this);
			}
		}
		ThemedModuleWidget<MidiKitModule>::onHoverKey(e);
	}

	void pasteJsClipboard() {
		const char* script = glfwGetClipboardString(APP->window->win);
		module->loadScript(script);
	}

	void copyJsClipboard() {
		const char* script = module->script.c_str();
		glfwSetClipboardString(APP->window->win, script);
	}
};


} // namespace MidiKit
} // namespace StoermelderPackOne

Model* modelMidiKit = createModel<StoermelderPackOne::MidiKit::MidiKitModule, StoermelderPackOne::MidiKit::MidiKitWidget>("MidiKit");