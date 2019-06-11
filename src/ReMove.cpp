#include "plugin.hpp"
#include "MapModule.hpp"
#include <thread>


const int MAX_DATA = 48000 * 2;     // 32 seconds, 16th precision at 48kHz
const int MAX_SEQ = 8;

struct ReMove : MapModule<1> {
    enum ParamIds {
        PLAY_PARAM,
        RESET_PARAM,
        REC_PARAM,
        SEQP_PARAM,
        SEQN_PARAM,
        NUM_PARAMS
    };
    enum InputIds {
        PLAY_INPUT,
        RESET_INPUT,
        POS_INPUT,
        SEQ_INPUT,
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
    /** stores the current position in data */
    int dataPtr = 0;            

    /** [Stored to JSON] number of sequences */
    int seqCount = 4;
    /** [Stored to JSON] currently selected sequence */
    int seq = 0;
    int seqLow;
    int seqHigh;
    /** [Stored to JSON] length of the seqences */
    int seqLength[MAX_SEQ];

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

    int sampleRate;

    dsp::SchmittTrigger seqPTrigger;
    dsp::SchmittTrigger seqNTrigger;
    dsp::BooleanTrigger playTrigger;
    dsp::SchmittTrigger resetTrigger;
    dsp::BooleanTrigger recTrigger;

	dsp::ClockDivider lightDivider;

    ReMove() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS); 
        configParam(SEQP_PARAM, 0.0f, 1.0f, 0.0f, "Previous sequence");
        configParam(SEQN_PARAM, 0.0f, 1.0f, 0.0f, "Next sequence");
        configParam(PLAY_PARAM, 0.0f, 1.0f, 0.0f, "Play");
        configParam(RESET_PARAM, 0.0f, 1.0f, 0.0f, "Reset");
        configParam(REC_PARAM, 0.0f, 1.0f, 0.0f, "Record");

        paramHandles[0].color = nvgRGB(0x40, 0xff, 0xff);
        paramHandles[0].text = "ReMove Light";

		lightDivider.setDivision(1024);
        onReset();
    }

    void onReset() override {
        MapModule::onReset();
        precisionCount = 0;        
        isPlaying = false;
        playDir = 1;
        isRecording = false;   
        recTouched = false;      
        dataPtr = 0;
        for (int i = 0; i < MAX_SEQ; i++) seqLength[i] = 0;
        seqUpdate();   

        valueFilters[0].reset();
    }

    void process(const ProcessArgs &args) override { 
        sampleRate = args.sampleRate;

        // Toggle record when button is pressed
        if (recTrigger.process(params[REC_PARAM].getValue())) {
            isPlaying = false;
            ParamQuantity *paramQuantity = getParamQuantity(0);
            if (paramQuantity != NULL) {
                isRecording ^= true;
                dataPtr = seqLow;
                precisionCount = 0;
                if (isRecording) {
                    seqLength[seq] = 0;
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
                    seqLength[seq]++;
                    // stop recording when store is full
                    if (dataPtr == seqHigh) {
                        isRecording = false;
                        params[REC_PARAM].setValue(0);
                    }
                }
                precisionCount = (precisionCount + 1) % (int)pow(2, precision);
            }
        }

        if (!isRecording && seqPTrigger.process(params[SEQP_PARAM].getValue())) {
            seqPrev();
        }

        if (!isRecording && seqNTrigger.process(params[SEQN_PARAM].getValue())) {
            seqNext();
        }

        // Reset ptr when button is pressed or input is triggered
        if (!isRecording && resetTrigger.process(params[RESET_PARAM].getValue() + inputs[RESET_INPUT].getVoltage())) {
            dataPtr = seqLow;
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
                int pos = floor(rescale(v, 0.f, 10.f, seqLow, seqLow + seqLength[seq] - 1));
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
                    if (dataPtr == seqLow + seqLength[seq] && playDir == 1) {
                        switch (playMode) {
                            case 0: dataPtr = seqLow; break;            // loop
                            case 1: dataPtr--; break;                   // oneshot, stay on last value
                            case 2: dataPtr--; playDir = -1; break;     // pingpong, reverse direction
                        }
                    }
                    if (dataPtr == seqLow - 1) {
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

    void seqNext() {
        seq = std::min(seq + 1, seqCount - 1);
        seqUpdate();
    }

    void seqPrev() {
        seq = std::max(seq - 1, 0);
        seqUpdate();
    }

    void seqSet(int c) {
        if (isRecording) return;
        isPlaying = false;
        seq = 0;
        seqCount = c;
        for (int i = 0; i < seqCount; i++) seqLength[i] = 0;        
        seqUpdate();
    }

    void seqUpdate() {
        int s = MAX_DATA / seqCount;    
        seqLow = seq * s;
        seqHigh =  (seq + 1) * s;
        dataPtr = seqLow;
        valueFilters[0].reset();        
    }


    void clearMap(int id) override {
        onReset();
        MapModule::clearMap(id);
    }

    void enableLearn(int id) override {
        if (isRecording) return;
        MapModule::enableLearn(id);
    }

    json_t *dataToJson() override {
		json_t *rootJ = MapModule::dataToJson();

        int s = MAX_DATA / seqCount;
		json_t *seqDataJ = json_array();
		for (int i = 0; i < seqCount; i++) {
            json_t *seqData1J = json_array();
            for (int j = 0; j < seqLength[i]; j++) {
                json_t *d = json_real(data[i * s + j]);
                json_array_append(seqData1J, d);
            }
			json_array_append(seqDataJ, seqData1J);
		}
		json_object_set_new(rootJ, "seqData", seqDataJ);

		json_t *seqLengthJ = json_array();
		for (int i = 0; i < seqCount; i++) {
			json_t *d = json_integer(seqLength[i]);
			json_array_append(seqLengthJ, d);
		}
		json_object_set_new(rootJ, "seqLength", seqLengthJ);

        json_object_set_new(rootJ, "seqCount", json_integer(seqCount));
        json_object_set_new(rootJ, "seq", json_integer(seq));
        json_object_set_new(rootJ, "recMode", json_integer(recMode));
        json_object_set_new(rootJ, "playMode", json_integer(playMode));
		json_object_set_new(rootJ, "precision", json_integer(precision));
        json_object_set_new(rootJ, "isPlaying", json_boolean(isPlaying));

		return rootJ;
	}

 	void dataFromJson(json_t *rootJ) override {
        MapModule::dataFromJson(rootJ);

    	json_t *seqCountJ = json_object_get(rootJ, "seqCount");
		if (seqCountJ) seqCount = json_integer_value(seqCountJ);
    	json_t *seqJ = json_object_get(rootJ, "seq");
		if (seqJ) seq = json_integer_value(seqJ);
    	json_t *recModeJ = json_object_get(rootJ, "recMode");
		if (recModeJ) recMode = json_integer_value(recModeJ);
    	json_t *playModeJ = json_object_get(rootJ, "playMode");
		if (playModeJ) playMode = json_integer_value(playModeJ);
    	json_t *precisionJ = json_object_get(rootJ, "precision");
		if (precisionJ) precision = json_integer_value(precisionJ);
    	json_t *isPlayingJ = json_object_get(rootJ, "isPlaying");
		if (isPlayingJ) isPlaying = json_boolean_value(isPlayingJ);

        json_t *seqLengthJ = json_object_get(rootJ, "seqLength");
		if (seqLengthJ) {
			json_t *d;
			size_t i;
			json_array_foreach(seqLengthJ, i, d) {
                if ((int)i >= seqCount) continue;
                seqLength[i] = json_integer_value(d);
			}
		}

        int s = MAX_DATA / seqCount;
        json_t *seqDataJ = json_object_get(rootJ, "seqData");
		if (seqDataJ) {
			json_t *seqData1J, *d;
			size_t i;
			json_array_foreach(seqDataJ, i, seqData1J) {
                if ((int)i >= seqCount) continue;
                size_t j;
                json_array_foreach(seqData1J, j, d) {
                    if ((int)j > seqLength[i]) continue;
                    data[i * s + j] = json_real_value(d);
                }
			}
		}

        seqUpdate();
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

        addInput(createInputCentered<PJ301MPort>(Vec(52.6f, 188.1f), module, ReMove::PLAY_INPUT));
        addParam(createParamCentered<LEDButton>(Vec(52.6f, 213.5f), module, ReMove::PLAY_PARAM));
        addChild(createLightCentered<MediumLight<GreenLight>>(Vec(52.6f, 213.5f), module, ReMove::PLAY_LIGHT));

        addInput(createInputCentered<PJ301MPort>(Vec(22.5f, 188.1f), module, ReMove::RESET_INPUT));
        addParam(createParamCentered<LEDButton>(Vec(22.5f, 213.5f), module, ReMove::RESET_PARAM));
        addChild(createLightCentered<MediumLight<GreenLight>>(Vec(22.5f, 213.5f), module, ReMove::RESET_LIGHT));

        addInput(createInputCentered<PJ301MPort>(Vec(37.6f, 251.6f), module, ReMove::POS_INPUT));
        addOutput(createOutputCentered<PJ301MPort>(Vec(37.6f, 336.3f), module, ReMove::CV_OUTPUT));

        addParam(createParamCentered<RecButton>(Vec(37.6f, 290.5f), module, ReMove::REC_PARAM));
        addChild(createLightCentered<RecLight>(Vec(37.6f, 290.5f), module, ReMove::REC_LIGHT));

        //addInput(createInputCentered<PJ301MPort>(Vec(37.6f, 146.f), module, ReMove::SEQ_INPUT));
        addParam(createParamCentered<TL1105>(Vec(19.9f, 127.9f), module, ReMove::SEQP_PARAM));
        addParam(createParamCentered<TL1105>(Vec(55.1f, 127.9f), module, ReMove::SEQN_PARAM));

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
                int s1 = MAX_DATA / module->sampleRate * pow(2, precision);
                int s2 = s1 / module->seqCount;
                rightText = string::f(((module->precision == precision) ? "✔ %ds / %ds" : "%ds / %ds"), s1, s2);
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


        struct SeqCountItem : MenuItem {
            ReMove *module;
            int seqCount;

            void onAction(const event::Action &e) override {
                module->seqSet(seqCount);
            }

            void step() override {
                rightText = (module->seqCount == seqCount) ? "✔" : "";
                MenuItem::step();
            }
        };

        struct SeqCountMenuItem : MenuItem {
            ReMove *module;

            Menu *createChildMenu() override {
                Menu *menu = new Menu;
                std::vector<std::string> names = {"1", "2", "4", "8"};
                for (size_t i = 0; i < names.size(); i++) {
                    menu->addChild(construct<SeqCountItem>(&MenuItem::text, names[i], &SeqCountItem::module, module, &SeqCountItem::seqCount, (int)pow(2, i)));
                }
                return menu;
            }
        };

        SeqCountMenuItem *seqCountMenuItem = construct<SeqCountMenuItem>(&MenuItem::text, "No of sequences", &SeqCountMenuItem::module, module);
        seqCountMenuItem->rightText = RIGHT_ARROW;
        menu->addChild(seqCountMenuItem);


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
                std::vector<std::string> names = {"First Move", "Instant"};
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