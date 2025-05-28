#include "MidiKit.h"
#include "MidiKitElk.cpp"
#include "../../components/Knobs.hpp"
#include "../../components/MidiWidget.hpp"
#include "../../components/LogDisplay.hpp"
#include "../../ui/OverlayMessageWidget.hpp"
#include <osdialog.h>
#include <fstream>
#include <queue>

namespace StoermelderPackOne {
namespace MidiKit {

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

	void send(Message& msg, uint64_t tick) {
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
			if (frame > s.msg.frame) {
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
			if (tick == s.tick) {
				tickQueue.pop();
				sendMessage(s.msg);
			}
			else {
				return;
			}
		}
	}
};

struct ScriptEnginePortInfo : PortInfo {
	bool enabled;
	ScriptEngine** se;

	std::string getName() override {
		return enabled ? (*se)->getInputName(portId) : "<Disabled>";
	}
};

struct ScriptEngineParamQuantity : ParamQuantity {
	bool enabled;
	ScriptEngine** se;

	std::string getLabel() override {
		return enabled ? (*se)->getParamName(paramId) : "";
	}
	std::string getDisplayValueString() override {
		return enabled ? (*se)->getParamFormatValue(paramId) : "<Disabled>";
	}
};


struct MidiKitModule : Module {
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

	ScriptEngine* se;
	dsp::RingBuffer<std::tuple<LOG_FORMAT, float, std::string>, 512> midiLogMessages;

	dsp::RingBuffer<int, 8> overlayQueue;
	std::tuple<std::string, std::string, std::string> overlayMessage;

	dsp::ClockDivider processDivider;
	dsp::SchmittTrigger trig;
	dsp::Timer rateLimiterTimer;

	uint64_t sample;
	uint64_t trigTick;

	MidiKitModule() {
		panelTheme = pluginSettings.panelThemeDefault;
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configInput(INPUT_TRIG, "Trigger");
		for (int i = 0; i < 4; i++) {
			configInput<ScriptEnginePortInfo>(INPUT + i)->se = &se;
			configParam<ScriptEngineParamQuantity>(PARAM + i, 0.f, 1.f, 0.f)->se = &se;
		}

		processDivider.setDivision(8);
		initEngine();
		onReset();
	}

	~MidiKitModule() {
		delete se;
	}

	void onReset() override {
		midiInput.reset();
		midiOutput.reset();
		sample = 0;
		trigTick = 0;
		for (int i = 0; i < 4; i++) {
			reinterpret_cast<ScriptEnginePortInfo*>(inputInfos[i])->enabled = false;
			reinterpret_cast<ScriptEngineParamQuantity*>(paramQuantities[i])->enabled = false;
		}
		midiLogMessages.push(std::make_tuple(LOG_FORMAT::RESET, 0.f, ""));
		se->loadScript("");
	}

	void initEngine() {
		se = new Elk::ElkScriptEngine;
		se->inputCount = 4;
		se->trigCount = 1;
		se->paramCount = 4;
		se->midiInputCount = 1;
		se->midiOutputCount = 1;

		se->logCallback = [=](std::string log) {
			float timestamp = float(sample) / APP->engine->getSampleRate();
			midiLogMessages.push(std::make_tuple(LOG_FORMAT::TIMESTAMP, timestamp, log));
		};
		se->overlayCallback = [=](std::string s1, std::string s2, std::string s3) {
			overlayQueue.push(0);
			overlayMessage = std::make_tuple(s1, s2, s3);
		};
		se->midiCallback = [=](int midiPort, Message& msg, uint64_t trigTick) {
			midiOutput.send(msg, trigTick);
		};

		se->inputEnable = [=](int i) {
			reinterpret_cast<ScriptEnginePortInfo*>(inputInfos[i])->enabled = true;
		};
		se->inputGetVoltage = [=](int i, uint8_t ch) {
			if (reinterpret_cast<ScriptEnginePortInfo*>(inputInfos[i])->enabled)
				return inputs[INPUT + i].getVoltage(ch);
			else
				return 0.f;
		};

		se->trigGetVoltage = [=](int i) {
			return inputs[INPUT_TRIG + i].getVoltage();
		};
		se->trigGetTicks = [=](int i) {
			return trigTick;
		};

		se->paramEnable = [=](int i) {
			reinterpret_cast<ScriptEngineParamQuantity*>(paramQuantities[i])->enabled = true;
		};
		se->paramGetValue = [=](int i) {
			if (reinterpret_cast<ScriptEngineParamQuantity*>(paramQuantities[i])->enabled)
				return params[PARAM + i].getValue();
			else
				return 0.f;
		};
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

		if (trig.process(inputs[INPUT_TRIG].getVoltage())) {
			trigTick++;
			midiOutput.processTick(trigTick);
		}

		if (processDivider.process()) {
			midi::Message msg;
			while (midiInput.tryPop(&msg, args.frame)) {
				se->process(0, msg);
			}

			midiOutput.processFrame(args.frame);
		}

		sample++;
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));

		json_object_set_new(rootJ, "midiInput", midiInput.toJson());
		json_object_set_new(rootJ, "midiOutput", midiOutput.toJson());
		json_object_set_new(rootJ, "script", json_string(script.c_str()));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		json_t* panelThemeJ = json_object_get(rootJ, "panelTheme");
		if (panelThemeJ) panelTheme = json_integer_value(panelThemeJ);

		json_t* midiInputJ = json_object_get(rootJ, "midiInput");
		if (midiInputJ) midiInput.fromJson(midiInputJ);
		json_t* midiOutputJ = json_object_get(rootJ, "midiOutput");
		if (midiOutputJ) midiOutput.fromJson(midiOutputJ);

		json_t* scriptJ = json_object_get(rootJ, "script");
		if (scriptJ) loadScript(json_string_value(scriptJ));
	}

	void loadScript(std::string s) {
		script = s;
		sample = 0;
		trigTick = 0;
		for (int i = 0; i < 4; i++) {
			reinterpret_cast<ScriptEnginePortInfo*>(inputInfos[i])->enabled = false;
			reinterpret_cast<ScriptEngineParamQuantity*>(paramQuantities[i])->enabled = false;
		}
		midiLogMessages.push(std::make_tuple(LOG_FORMAT::RESET, 0.f, ""));
		se->loadScript(script.c_str());
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

		LedDisplay* textDisplay = createWidget<LedDisplay>(Vec(0.f, 86.2f));
		textDisplay->box.size = Vec(180.f, 140.6f);
		addChild(textDisplay);

		logDisplay = createWidget<LogDisplay>(Vec());
		logDisplay->buffer = &buffer;
		logDisplay->box.size = textDisplay->box.size.minus(Vec(0.f, 6.f));
		logDisplay->fontSize = 7.2f;
		textDisplay->addChild(logDisplay);

		MidiWidget<>* display2 = createWidget<MidiWidget<>>(Vec(0.f, 232.1f));
		display2->box.size = Vec(180.0f, 44.6f);
		display2->setMidiPort(module ? &module->midiOutput : NULL, "Out");
		addChild(display2);

		addParam(createParamCentered<StoermelderTrimpot>(Vec(24.7f, 293.7f), module, MidiKitModule::PARAM + 0));
		addParam(createParamCentered<StoermelderTrimpot>(Vec(56.2f, 293.7f), module, MidiKitModule::PARAM + 1));
		addParam(createParamCentered<StoermelderTrimpot>(Vec(87.6f, 293.7f), module, MidiKitModule::PARAM + 2));
		addParam(createParamCentered<StoermelderTrimpot>(Vec(119.1f, 293.7f), module, MidiKitModule::PARAM + 3));

		addInput(createInputCentered<StoermelderPort>(Vec(24.7f, 328.4f), module, MidiKitModule::INPUT + 0));
		addInput(createInputCentered<StoermelderPort>(Vec(56.2f, 328.4f), module, MidiKitModule::INPUT + 1));
		addInput(createInputCentered<StoermelderPort>(Vec(87.6f, 328.4f), module, MidiKitModule::INPUT + 2));
		addInput(createInputCentered<StoermelderPort>(Vec(119.1f, 328.4f), module, MidiKitModule::INPUT + 3));

		addInput(createInputCentered<StoermelderPort>(Vec(156.8f, 328.4f), module, MidiKitModule::INPUT_TRIG));

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
		while (!module->midiLogMessages.empty()) {
			if (buffer.size() == BUFFERSIZE) buffer.pop_back();
			std::tuple<LOG_FORMAT, float, std::string> s = module->midiLogMessages.shift();
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
		menu->addChild(new MenuSeparator());
		menu->addChild(createMenuLabel("Script"));
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
		osdialog_filters* filters = osdialog_filters_parse("MIDI-SCRIPT file (.js):js");
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