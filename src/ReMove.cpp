#include "plugin.hpp"
#include "MapModule.hpp"
#include <thread>


const int MAX_DATA = 48000 * 2;     // 32 seconds, 16th precision at 48kHz


struct ReMove : MapModule<1> {
    enum ParamIds {
        PLAY_PARAM,
        RESET_PARAM,
        REC_PARAM,
        NUM_PARAMS
    };
    enum InputIds {
        PLAY_INPUT,
        RESET_INPUT,
        POS_INPUT,  
        NUM_INPUTS
    };
    enum OutputIds {
        CV_OUTPUT,
        NUM_OUTPUTS
    };
    enum LightIds {
        PLAY_LIGHT,
        RESET_LIGHT,
        REC_LIGHT,
        NUM_LIGHTS
    };

    /** [STORED TO JSON] */
    float data[MAX_DATA];
    /** [STORED TO JSON] */
    int dataLength;             // stores the length of the recording
    int dataPtr = 0;            // stores the current position in data

    /** [STORED TO JSON] */
    int recMode = 0;            // 0 = First Touch, 1 = Instant
    bool recTouched = false;
    float recTouch;

    /** [STORED TO JSON] */
    int precision = 7;          // rate for recording is 2^precision 
    int precisionCount = 0;

    /** [STORED TO JSON] */
    int playMode = 0;           // 0 = Loop, 1 = Oneshot, 2 = Pingpong
    int playDir = 1;

    /** [STORED TO JSON] */
    bool isPlaying = false;
    bool isRecording = false;

    dsp::BooleanTrigger playTrigger;
    dsp::SchmittTrigger resetTrigger;
    dsp::BooleanTrigger recTrigger;

	dsp::ClockDivider lightDivider;

    ReMove() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS); 
        configParam(PLAY_PARAM, 0.0f, 1.0f, 0.0f, "Play");
        configParam(RESET_PARAM, 0.0f, 1.0f, 0.0f, "Reset");
        configParam(REC_PARAM, 0.0f, 1.0f, 0.0f, "Record");

        paramHandles[0].color = nvgRGB(0x40, 0xff, 0xff);
        paramHandles[0].text = "ReMove Light";

        onReset();
		lightDivider.setDivision(1024);
    }

    void onReset() override {
        MapModule::onReset();
        playDir = 1;
        dataPtr = 0;
        dataLength = 0;
        precisionCount = 0;
        recTouched = false;
        valueFilters[0].reset();
    }

    void process(const ProcessArgs &args) override { 
        // Toggle record when button is pressed
        if (recTrigger.process(params[REC_PARAM].getValue())) {
            isPlaying = false;
            ParamQuantity *paramQuantity = getParamQuantity(0);
            if (paramQuantity != NULL) {
                isRecording ^= true;
                dataPtr = 0;
                precisionCount = 0;
                if (isRecording) {
                    dataLength = 0;
                    paramHandles[0].color = nvgRGB(0xff, 0x40, 0xff);
                    recTouch = paramQuantity->getScaledValue();
                    recTouched = false;
                } else {
                    paramHandles[0].color = nvgRGB(0x40, 0xff, 0xff);
                    valueFilters[0].reset();       
                }
            }
        }

        if (isRecording) {
            bool doRecord = true;

            // In case of record mode "Touch" check if param value has changed
            if (recMode == 0 && !recTouched) {
                ParamQuantity *paramQuantity = getParamQuantity(0);
                float v = paramQuantity->getScaledValue();
                if (v != recTouch) {
                    recTouched = true;
                } else {
                    doRecord = false;
                }
            }

            if (doRecord) {
                if (precisionCount == 0) {
                    ParamQuantity *paramQuantity = getParamQuantity(0);
                    float v = paramQuantity->getScaledValue();

                    data[dataPtr] = v;
                    dataPtr++;
                    dataLength++;
                    // stop recording when store is full
                    if (dataPtr == MAX_DATA) {
                        isRecording = false;
                        params[REC_PARAM].setValue(0);
                    }
                }
                precisionCount = (precisionCount + 1) % (int)pow(2, precision);
            }
        }

        // Reset ptr when button is pressed of input is triggered
        if (!isRecording && resetTrigger.process(params[RESET_PARAM].getValue() + inputs[RESET_INPUT].getVoltage())) {
            dataPtr = 0;
            playDir = 1;
            precisionCount = 0;
            valueFilters[0].reset();
        }

        // Toggle playing when button is pressed
        if (!isRecording && playTrigger.process(params[PLAY_PARAM].getValue())) {
            isPlaying ^= true;
            precisionCount = 0;
        }

        // Set playing when input is high
		if (!isRecording && inputs[PLAY_INPUT].isConnected()) {
			isPlaying = (inputs[PLAY_INPUT].getVoltage() >= 2.f);
		}

        // If position-input is connected set the position directly, ignore playing
        if (!isRecording && inputs[POS_INPUT].isConnected()) {
            isPlaying = false;
            ParamQuantity *paramQuantity = getParamQuantity(0);
            if (paramQuantity != NULL) {
                float v = clamp(inputs[POS_INPUT].getVoltage(), 0.f, 10.f);
                int pos = floor(rescale(v, 0.f, 10.f, 0, dataLength - 1));
                v = data[pos];
                paramQuantity->setScaledValue(v);
                if (outputs[CV_OUTPUT].isConnected()) {
                    v = rescale(v, 0.f, 1.f, 0.f, 10.f);
                    outputs[CV_OUTPUT].setVoltage(v);
                }  
            }     
        }

        if (isPlaying) {
            if (precisionCount == 0) {
                ParamQuantity *paramQuantity = getParamQuantity(0);
                if (paramQuantity == NULL) {
                    isPlaying = false;

                } else {
                    float v = data[dataPtr];
                    v = valueFilters[0].process(args.sampleTime, v);
                    paramQuantity->setScaledValue(v);
                    dataPtr = dataPtr + playDir;
                    if (outputs[CV_OUTPUT].isConnected()) {
                        v = rescale(v, 0.f, 1.f, 0.f, 10.f);
                        outputs[CV_OUTPUT].setVoltage(v);
                    }
                    if (dataPtr == dataLength && playDir == 1) {
                        switch (playMode) {
                            case 0: dataPtr = 0; break;                 // loop
                            case 1: dataPtr = dataLength - 1; break;    // oneshot, stay on last value
                            case 2: dataPtr--; playDir = -1; break;     // pingpong, reverse direction
                        }
                    }
                    if (dataPtr == -1) {
                        dataPtr++; playDir = 1;
                    }
                }
            }
            precisionCount = (precisionCount + 1) % (int)pow(2, precision);
        }

		// Set channel lights infrequently
		if (lightDivider.process()) {
            lights[PLAY_LIGHT].setBrightness(isPlaying);
            lights[RESET_LIGHT].setSmoothBrightness(resetTrigger.isHigh(), lightDivider.getDivision() * args.sampleTime);
            lights[REC_LIGHT].setBrightness(isRecording);
        }

        MapModule::process(args);
    }

    void clearMap(int id) override {
        dataLength = 0;
        isPlaying = false;
        isRecording = false;
        MapModule::clearMap(id);
    }

    void enableLearn(int id) override {
        if (isRecording) return;
        MapModule::enableLearn(id);
    }

    json_t *dataToJson() override {
		json_t *rootJ = MapModule::dataToJson();

		json_t *dataJ = json_array();
		for (int i = 0; i < dataLength; i++) {
			json_t *d = json_real(data[i]);
			json_array_append(dataJ, d);
		}
		json_object_set_new(rootJ, "data", dataJ);

        json_object_set_new(rootJ, "dataLength", json_integer(dataLength));
        json_object_set_new(rootJ, "recMode", json_integer(recMode));
        json_object_set_new(rootJ, "playMode", json_integer(playMode));
		json_object_set_new(rootJ, "precision", json_integer(precision));
        json_object_set_new(rootJ, "isPlaying", json_boolean(isPlaying));

		return rootJ;
	}

 	void dataFromJson(json_t *rootJ) override {
        MapModule::dataFromJson(rootJ);
		
    	json_t *dataLengthJ = json_object_get(rootJ, "dataLength");
		if (dataLengthJ) dataLength = json_integer_value(dataLengthJ);
    	json_t *recModeJ = json_object_get(rootJ, "recMode");
		if (recModeJ) recMode = json_integer_value(recModeJ);
    	json_t *playModeJ = json_object_get(rootJ, "playMode");
		if (playModeJ) playMode = json_integer_value(playModeJ);
    	json_t *precisionJ = json_object_get(rootJ, "precision");
		if (precisionJ) precision = json_integer_value(precisionJ);
    	json_t *isPlayingJ = json_object_get(rootJ, "isPlaying");
		if (isPlayingJ) isPlaying = json_boolean_value(isPlayingJ);

        json_t *dataJ = json_object_get(rootJ, "data");
		if (dataJ) {
			json_t *d;
			size_t dataIndex;
			json_array_foreach(dataJ, dataIndex, d) {
                if (dataIndex >= MAX_DATA) continue;
                data[dataIndex] = json_real_value(d);
			}
		}
	}   
};


struct RecButton : SvgSwitch {
    RecButton() {
        momentary = true;
        box.size = Vec(28.f, 28.f);
        addFrame(APP->window->loadSvg(asset::plugin(pluginInstance, "res/RecButton.svg")));
    }
};

struct RecLight : RedLight {
	RecLight() {
		bgColor = nvgRGB(0x66, 0x66, 0x66);
		box.size = Vec(20.f, 20.f);
	}
};


struct ReMoveWidget : ModuleWidget {
    ReMoveWidget(ReMove *module) {	
        setModule(module);
        setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/ReMove.svg")));

        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        addInput(createInputCentered<PJ301MPort>(Vec(37.6f, 121.7f), module, ReMove::PLAY_INPUT));
        addParam(createParamCentered<LEDButton>(Vec(37.6f, 147.6f), module, ReMove::PLAY_PARAM));
        addChild(createLightCentered<MediumLight<GreenLight>>(Vec(37.6f, 147.6f), module, ReMove::PLAY_LIGHT));

        addInput(createInputCentered<PJ301MPort>(Vec(37.6f, 187.f), module, ReMove::RESET_INPUT));
        addParam(createParamCentered<LEDButton>(Vec(37.6f, 211.9f), module, ReMove::RESET_PARAM));
        addChild(createLightCentered<MediumLight<GreenLight>>(Vec(37.6f, 211.9f), module, ReMove::RESET_LIGHT));

        addInput(createInputCentered<PJ301MPort>(Vec(37.6f, 255.3f), module, ReMove::POS_INPUT));
        addOutput(createOutputCentered<PJ301MPort>(Vec(37.6f, 335.9f), module, ReMove::CV_OUTPUT));

        addParam(createParamCentered<RecButton>(Vec(37.6f, 294.7f), module, ReMove::REC_PARAM));
        addChild(createLightCentered<RecLight>(Vec(37.6f, 294.7f), module, ReMove::REC_LIGHT));

		MapModuleDisplay<1> *mapWidget = createWidget<MapModuleDisplay<1>>(Vec(6.8f, 36.4f));
		mapWidget->box.size = Vec(61.5f, 23.5f);
		mapWidget->setModule(module);
		addChild(mapWidget);
    }

    void appendContextMenu(Menu *menu) override {
        ReMove *module = dynamic_cast<ReMove*>(this->module);
        assert(module);

        struct ManualItem : MenuItem {
            void onAction(const event::Action &e) override {
                std::thread t(system::openBrowser, "https://github.com/stoermelder/vcvrack-packone/blob/v1/docs/ReMove.md");
                t.detach();
            }
        };

        menu->addChild(construct<ManualItem>(&MenuItem::text, "Module Manual"));
        menu->addChild(construct<MenuLabel>());

        struct PrecisionItem : MenuItem {
            ReMove *module;
            int precision;

            void onAction(const event::Action &e) override {
                module->precision = precision;
            }

            void step() override {
                rightText = (module->precision == precision) ? "✔" : "";
                MenuItem::step();
            }
        };

        struct PrecisionMenuItem : MenuItem {
            ReMove *module;

            Menu *createChildMenu() override {
                Menu *menu = new Menu;
                std::vector<std::string> names = {"16th", "32nd", "64th", "128th", "256th", "512nd", "1024th"};
                for (size_t i = 0; i < names.size(); i++) {
                    menu->addChild(construct<PrecisionItem>(&MenuItem::text, names[i], &PrecisionItem::module, module, &PrecisionItem::precision, i + 4));
                }
                return menu;
            }
        };

        PrecisionMenuItem *precisionMenuItem = construct<PrecisionMenuItem>(&MenuItem::text, "Precision", &PrecisionMenuItem::module, module);
        precisionMenuItem->rightText = RIGHT_ARROW;
        menu->addChild(precisionMenuItem);

        struct RecordModeItem : MenuItem {
            ReMove *module;
            int recMode;

            void onAction(const event::Action &e) override {
                module->recMode = recMode;
            }

            void step() override {
                rightText = (module->recMode == recMode) ? "✔" : "";
                MenuItem::step();
            }
        };

        struct RecordModeMenuItem : MenuItem {
            ReMove *module;

            Menu *createChildMenu() override {
                Menu *menu = new Menu;
                std::vector<std::string> names = {"First Touch", "Instant"};
                for (size_t i = 0; i < names.size(); i++) {
                    menu->addChild(construct<RecordModeItem>(&MenuItem::text, names[i], &RecordModeItem::module, module, &RecordModeItem::recMode, i));
                }
                return menu;
            }
        };

        RecordModeMenuItem *recordModeMenuItem = construct<RecordModeMenuItem>(&MenuItem::text, "Record Mode", &RecordModeMenuItem::module, module);
        recordModeMenuItem->rightText = RIGHT_ARROW;
        menu->addChild(recordModeMenuItem); 

        struct PlayModeItem : MenuItem {
            ReMove *module;
            int playMode;

            void onAction(const event::Action &e) override {
                module->playMode = playMode;
            }

            void step() override {
                rightText = (module->playMode == playMode) ? "✔" : "";
                MenuItem::step();
            }
        };

        struct PlayModeMenuItem : MenuItem {
            ReMove *module;

            Menu *createChildMenu() override {
                Menu *menu = new Menu;
                std::vector<std::string> names = {"Loop", "Oneshot", "Ping Pong"};
                for (size_t i = 0; i < names.size(); i++) {
                    menu->addChild(construct<PlayModeItem>(&MenuItem::text, names[i], &PlayModeItem::module, module, &PlayModeItem::playMode, i));
                }
                return menu;
            }
        };

        PlayModeMenuItem *playModeMenuItem = construct<PlayModeMenuItem>(&MenuItem::text, "Play Mode", &PlayModeMenuItem::module, module);
        playModeMenuItem->rightText = RIGHT_ARROW;
        menu->addChild(playModeMenuItem);         
    }
};


Model *modelReMoveLight = createModel<ReMove, ReMoveWidget>("ReMoveLight");