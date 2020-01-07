#include "plugin.hpp"


namespace MidiStep {

enum MODE {
	BEATSTEP_R1 = 0,
	BEATSTEP_R2 = 1,
	KK_REL = 10
};

template < int PORTS = 8 >
struct MidiStepModule : Module {
	enum ParamIds {
		NUM_PARAMS
	};
	enum InputIds {
		NUM_INPUTS
	};
	enum OutputIds {
		ENUMS(OUTPUT_INC, PORTS),
		ENUMS(OUTPUT_DEC, PORTS),
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	/** [Stored to JSON] */
	int panelTheme = 0;
	/** [Stored to JSON] */
	midi::InputQueue midiInput;
	/** [Stored to JSON] */
	MODE mode = MODE::BEATSTEP_R1;

	/** [Stored to JSON] */
	int learnedCcs[PORTS];
	int learningId;

	int8_t values[128];
    int ccs[128];

	int incPulseCount[PORTS];
	dsp::PulseGenerator incPulse[PORTS];
	int decPulseCount[PORTS];
	dsp::PulseGenerator decPulse[PORTS];

	MidiStepModule() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		onReset();
	}

	void onReset() override {
		for (int i = 0; i < 128; i++) {
            values[i] = 0;
			ccs[i] = -1;
		}
		for (int i = 0; i < PORTS; i++) {
			learnedCcs[i] = i;
            ccs[i] = i;
			incPulseCount[i] = 0;
			decPulseCount[i] = 0;
		}
		learningId = -1;
		midiInput.reset();
	}

	void process(const ProcessArgs& args) override {
		midi::Message msg;
		while (midiInput.shift(&msg)) {
			processMessage(msg);
		}

		for (int i = 0; i < PORTS; i++) {
            if (incPulse[i].process(args.sampleTime)) {
                outputs[OUTPUT_INC + i].setVoltage(incPulseCount[i] % 2 == 1 ? 10.f : 0.f);
            }
            else {
                if (incPulseCount[i] > 0) {
                    incPulse[i].trigger();
                    incPulseCount[i]--;
                }
                outputs[OUTPUT_INC + i].setVoltage(0.f);
            }

            if (decPulse[i].process(args.sampleTime)) {
                outputs[OUTPUT_DEC + i].setVoltage(decPulseCount[i] % 2 == 1 ? 10.f : 0.f);
            }
            else {
                if (decPulseCount[i] > 0) {
                    decPulse[i].trigger();
                    decPulseCount[i]--;
                }
                outputs[OUTPUT_DEC + i].setVoltage(0.f);
            }
		}
	}

	void processMessage(midi::Message msg) {
		switch (msg.getStatus()) {
			// cc
			case 0xb: {
				processCC(msg);
                break;
			} 
		}
	}

	void processCC(midi::Message msg) {
		uint8_t cc = msg.getNote();

		int8_t value = msg.bytes[2];
		value = clamp(value, 0, 127);
		// Learn
		if (learningId >= 0) {
            if (learnedCcs[learningId] >= 0) {
                ccs[learnedCcs[learningId]] = -1;
            }
            ccs[cc] = learningId;
			learnedCcs[learningId] = cc;
			learningId = -1;
			return;
		}

        switch (mode) {
            case MODE::BEATSTEP_R1: {
                if (value == uint8_t(60)) decPulseCount[ccs[cc]] += 6;
                if (value == uint8_t(61)) decPulseCount[ccs[cc]] += 4;
                if (value == uint8_t(62)) decPulseCount[ccs[cc]] += 2;
                if (value == uint8_t(64)) incPulseCount[ccs[cc]] += 2;
                if (value == uint8_t(65)) incPulseCount[ccs[cc]] += 4;
                if (value == uint8_t(66)) incPulseCount[ccs[cc]] += 6;
                break;
            }

            case MODE::BEATSTEP_R2:
            case MODE::KK_REL: {
                if (value == uint8_t(127)) decPulseCount[ccs[cc]] += 2;
                if (value == uint8_t(1)) incPulseCount[ccs[cc]] += 2;
                break;
            }
        }

		values[cc] = value;
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));
		json_object_set_new(rootJ, "mode", json_integer(mode));

		json_t* ccsJ = json_array();
		for (int i = 0; i < PORTS; i++) {
			json_array_append_new(ccsJ, json_integer(learnedCcs[i]));
		}
		json_object_set_new(rootJ, "ccs", ccsJ);

		json_object_set_new(rootJ, "midi", midiInput.toJson());
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		for (int i = 0; i < 128; i++) {
			ccs[i] = -1;
		}

		panelTheme = json_integer_value(json_object_get(rootJ, "panelTheme"));
		mode = (MODE)json_integer_value(json_object_get(rootJ, "mode"));

		json_t* ccsJ = json_object_get(rootJ, "ccs");
		if (ccsJ) {
			for (int i = 0; i < PORTS; i++) {
				json_t* ccJ = json_array_get(ccsJ, i);
				if (ccJ) {
					learnedCcs[i] = json_integer_value(ccJ);
					ccs[learnedCcs[i]] = i;
				}
			}
		}

		json_t* midiJ = json_object_get(rootJ, "midi");
		if (midiJ) midiInput.fromJson(midiJ);
	}
};


struct MyMidiWidget : MidiWidget {
	void setMidiPort(midi::Port *port) {
		MidiWidget::setMidiPort(port);

		driverChoice->textOffset = Vec(6.f, 14.7f);
		driverChoice->box.size = mm2px(Vec(driverChoice->box.size.x, 7.5f));
		driverChoice->color = nvgRGB(0xf0, 0xf0, 0xf0);

		driverSeparator->box.pos = driverChoice->box.getBottomLeft();

		deviceChoice->textOffset = Vec(6.f, 14.7f);
		deviceChoice->box.size = mm2px(Vec(deviceChoice->box.size.x, 7.5f));
		deviceChoice->box.pos = driverChoice->box.getBottomLeft();
		deviceChoice->color = nvgRGB(0xf0, 0xf0, 0xf0);

		deviceSeparator->box.pos = deviceChoice->box.getBottomLeft();

		channelChoice->textOffset = Vec(6.f, 14.7f);
		channelChoice->box.size = mm2px(Vec(channelChoice->box.size.x, 7.5f));
		channelChoice->box.pos = deviceChoice->box.getBottomLeft();
		channelChoice->color = nvgRGB(0xf0, 0xf0, 0xf0);
	}
};


template < int PORTS, class COICE >
struct MyLedWidget : LedDisplay {
	LedDisplaySeparator* hSeparators[PORTS / 4];
	LedDisplaySeparator* vSeparators[4];
	COICE* choices[4][PORTS / 4];

	void setModule(MidiStepModule<>* module) {
		Vec pos = Vec(0, 0);

		LedDisplaySeparator* channelSeparator = createWidget<LedDisplaySeparator>(pos);
		channelSeparator->box.size.x = box.size.x;
		addChild(channelSeparator);

		// Add vSeparators
		for (int x = 1; x < 4; x++) {
			vSeparators[x] = createWidget<LedDisplaySeparator>(pos);
			vSeparators[x]->box.pos.x = box.size.x / 4 * x;
			addChild(vSeparators[x]);
		}
		// Add hSeparators and choice widgets
		for (int y = 0; y < PORTS / 4; y++) {
			hSeparators[y] = createWidget<LedDisplaySeparator>(pos);
			hSeparators[y]->box.size.x = box.size.x;
			addChild(hSeparators[y]);
			for (int x = 0; x < 4; x++) {
				choices[x][y] = new COICE;
				choices[x][y]->box.pos = pos;
				choices[x][y]->setId(4 * y + x);
				choices[x][y]->box.size.x = box.size.x / 4;
				choices[x][y]->box.pos.x = box.size.x / 4 * x;
				choices[x][y]->setModule(module);
				addChild(choices[x][y]);
			}
			pos = choices[0][y]->box.getBottomLeft();
		}
		for (int x = 1; x < 4; x++) {
			vSeparators[x]->box.size.y = pos.y - vSeparators[x]->box.pos.y;
		}
	}
};


struct MyCcChoice : LedDisplayChoice {
	MidiStepModule<>* module;
	int id;
	int focusCc;

	MyCcChoice() {
		box.size.y = mm2px(6.666);
		textOffset.y -= 4.f;
		textOffset.x -= 2.5f;
		color = nvgRGB(0xf0, 0xf0, 0xf0);
	}

	void setModule(MidiStepModule<>* module) {
		this->module = module;
	}

	void setId(int id) {
		this->id = id;
	}

	void step() override {
		if (!module) {
			text = string::f("%03d", id);
			return;
		}
		
		if (module->learningId == id) {
			if (0 <= focusCc)
				text = string::f("%03d", focusCc);
			else
				text = "---";
			color.a = 0.5;
		}
		else {
			text = string::f("%03d", module->learnedCcs[id]);
			color.a = 1.0;
			// HACK
			if (APP->event->selectedWidget == this)
				APP->event->setSelected(NULL);
		}
	}

	void onButton(const event::Button &e) override {
		e.stopPropagating();
		if (!module)
			return;

		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			e.consume(this);
			module->learningId = id;
			focusCc = -1;
		}
	}

	void onDeselect(const event::Deselect& e) override {
		if (!module)
			return;
		if (module->learningId == id) {
			if (0 <= focusCc && focusCc < 128) {
				module->learnedCcs[id] = focusCc;
			}
			module->learningId = -1;
		}
	}

	void onSelectText(const event::SelectText& e) override {
		int c = e.codepoint - '0';
		if (0 <= c && c <= 9) {
			if (focusCc < 0)
				focusCc = 0;
			focusCc = focusCc * 10 + c;
		}
		if (focusCc >= 128)
			focusCc = 0;
		e.consume(this);
	}

	void onSelectKey(const event::SelectKey& e) override {
		if ((e.key == GLFW_KEY_ENTER || e.key == GLFW_KEY_KP_ENTER) && e.action == GLFW_PRESS && (e.mods & RACK_MOD_MASK) == 0) {
			event::Deselect eDeselect;
			onDeselect(eDeselect);
			APP->event->selectedWidget = NULL;
			e.consume(this);
		}
	}
};

struct MidiStepWidget : ModuleWidget {
	MidiStepWidget(MidiStepModule<>* module) {
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/MidiStep.svg")));

		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		MyMidiWidget* midiInputWidget = createWidget<MyMidiWidget>(Vec(10.0f, 36.4f));
		midiInputWidget->box.size = Vec(130.0f, 67.0f);
		midiInputWidget->setMidiPort(module ? &module->midiInput : NULL);
		addChild(midiInputWidget);

		MyLedWidget<8, MyCcChoice>* midiWidget = createWidget<MyLedWidget<8, MyCcChoice>>(Vec(10.f, 108.7f));
		midiWidget->box.size = Vec(130.0f, 40.0f);
		midiWidget->setModule(module);
		addChild(midiWidget);

		addOutput(createOutputCentered<StoermelderPort>(Vec(27.9f, 232.7f), module, MidiStepModule<>::OUTPUT_INC + 0));
		addOutput(createOutputCentered<StoermelderPort>(Vec(56.3f, 232.7f), module, MidiStepModule<>::OUTPUT_INC + 1));
		addOutput(createOutputCentered<StoermelderPort>(Vec(92.6f, 232.7f), module, MidiStepModule<>::OUTPUT_INC + 2));
		addOutput(createOutputCentered<StoermelderPort>(Vec(122.1f, 232.7f), module, MidiStepModule<>::OUTPUT_INC + 3));
		addOutput(createOutputCentered<StoermelderPort>(Vec(27.9f, 261.0f), module, MidiStepModule<>::OUTPUT_INC + 4));
		addOutput(createOutputCentered<StoermelderPort>(Vec(56.3f, 261.0f), module, MidiStepModule<>::OUTPUT_INC + 5));
		addOutput(createOutputCentered<StoermelderPort>(Vec(92.6f, 261.0f), module, MidiStepModule<>::OUTPUT_INC + 6));
		addOutput(createOutputCentered<StoermelderPort>(Vec(122.1f, 261.0f), module, MidiStepModule<>::OUTPUT_INC + 7));

		addOutput(createOutputCentered<StoermelderPort>(Vec(27.9f, 298.8f), module, MidiStepModule<>::OUTPUT_DEC + 0));
		addOutput(createOutputCentered<StoermelderPort>(Vec(56.3f, 298.8f), module, MidiStepModule<>::OUTPUT_DEC + 1));
		addOutput(createOutputCentered<StoermelderPort>(Vec(92.6f, 298.8f), module, MidiStepModule<>::OUTPUT_DEC + 2));
		addOutput(createOutputCentered<StoermelderPort>(Vec(122.1f, 298.8f), module, MidiStepModule<>::OUTPUT_DEC + 3));
		addOutput(createOutputCentered<StoermelderPort>(Vec(27.9f, 327.1f), module, MidiStepModule<>::OUTPUT_DEC + 4));
		addOutput(createOutputCentered<StoermelderPort>(Vec(56.3f, 327.1f), module, MidiStepModule<>::OUTPUT_DEC + 5));
		addOutput(createOutputCentered<StoermelderPort>(Vec(92.6f, 327.1f), module, MidiStepModule<>::OUTPUT_DEC + 6));
		addOutput(createOutputCentered<StoermelderPort>(Vec(122.1f, 327.1f), module, MidiStepModule<>::OUTPUT_DEC + 7));
	}

	void appendContextMenu(Menu* menu) override {
        MidiStepModule<>* module = dynamic_cast<MidiStepModule<>*>(this->module);
		struct ModeMenuItem : MenuItem {
			MidiStepModule<>* module;
			ModeMenuItem() {
				rightText = RIGHT_ARROW;
			}

			Menu* createChildMenu() override {
				Menu* menu = new Menu;

				struct ModeItem : MenuItem {
					MidiStepModule<>* module;
					MODE mode;
					void onAction(const event::Action& e) override {
						module->mode = mode;
					}
					void step() override {
						rightText = module->mode == mode ? "✔" : "";
						MenuItem::step();
					}
				};

				menu->addChild(construct<ModeItem>(&MenuItem::text, "Beatstep Rel #1", &ModeItem::module, module, &ModeItem::mode, MODE::BEATSTEP_R1));
				menu->addChild(construct<ModeItem>(&MenuItem::text, "Beatstep Rel #2", &ModeItem::module, module, &ModeItem::mode, MODE::BEATSTEP_R2));
				menu->addChild(construct<ModeItem>(&MenuItem::text, "Komplete Kontrol Relative", &ModeItem::module, module, &ModeItem::mode, MODE::KK_REL));
				return menu;
			}
		};

        menu->addChild(new MenuSeparator());
		menu->addChild(construct<ModeMenuItem>(&MenuItem::text, "Mode", &ModeMenuItem::module, module));
    }
};

}

Model* modelMidiStep = createModel<MidiStep::MidiStepModule<>, MidiStep::MidiStepWidget>("MidiStep");
