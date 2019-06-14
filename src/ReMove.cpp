#include "plugin.hpp"
#include "MapModule.hpp"
#include <thread>


const int MAX_DATA = 48000 * 2;     // 32 seconds, 16th precision at 48kHz
const int MAX_SEQ = 8;

const int RECMODE_TOUCH = 0;
const int RECMODE_MOVE = 1;
const int RECMODE_MANUAL = 2;

struct ReMove : MapModule<1> {
    enum ParamIds {
        RUN_PARAM,
        RESET_PARAM,
        REC_PARAM,
        SEQP_PARAM,
        SEQN_PARAM,
        NUM_PARAMS
    };
    enum InputIds {
        RUN_INPUT,
        RESET_INPUT,
        PHASE_INPUT,
        SEQ_INPUT,
        CV_INPUT,
        NUM_INPUTS
    };
    enum OutputIds {
        CV_OUTPUT,
        NUM_OUTPUTS
    };
    enum LightIds {
        RUN_LIGHT,
        RESET_LIGHT,
        REC_LIGHT,
        ENUMS(SEQ_LIGHT, 8),
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

    /** [Stored to JSON] mode for SEQ CV input, 0 = 0-10V, 1 = C4-G4, 2 = Trig */
    int seqCvMode = 0;

    /** [Stored to JSON] recording mode */
    int recMode = RECMODE_TOUCH;
    bool recTouched = false;
    float recTouch;

    /** [Stored to JSON] rate for recording, interpreted as 2^precision */
    int precision = 7;          
    int precisionCount = 0;

    /** [Stored to JSON] mode for playback, 0 = Loop, 1 = Oneshot, 2 = Pingpong */
    int playMode = 0;
    int playDir = 1;

    /** [Stored to JSON] state of playback (for button-press manually) */
    bool isPlaying = false;
    bool isRecording = false;

    int sampleRate;

    dsp::SchmittTrigger seqPTrigger;
    dsp::SchmittTrigger seqNTrigger;
    dsp::SchmittTrigger seqCvTrigger;
    dsp::BooleanTrigger runTrigger;
    dsp::SchmittTrigger resetCvTrigger;
    dsp::BooleanTrigger recTrigger;

	dsp::ClockDivider lightDivider;

    ReMove() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS); 
        configParam(SEQP_PARAM, 0.0f, 1.0f, 0.0f, "Previous sequence");
        configParam(SEQN_PARAM, 0.0f, 1.0f, 0.0f, "Next sequence");
        configParam(RUN_PARAM, 0.0f, 1.0f, 0.0f, "Run");
        configParam(RESET_PARAM, 0.0f, 1.0f, 0.0f, "Reset");
        configParam(REC_PARAM, 0.0f, 1.0f, 0.0f, "Record");

        paramHandles[0].color = nvgRGB(0x40, 0xff, 0xff);
        paramHandles[0].text = "ReMove Lite";

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
                if (isRecording) 
                    startRecording();
                else
                    stopRecording();
            }
        }

        if (isRecording) {
            bool doRecord = true;

            if (recMode == RECMODE_TOUCH && !recTouched) {
                // check if mouse has been pressed on parameter
                if (APP->event->draggedWidget != NULL) {
                    // HACK! uses unstable API!
                    // it is not a good idea to do this in the DSP kernel, but the
                    // code is only executed when record is armed and something is dragged
                    // accross the rack, so it's OK...
                    ParamWidget *pw = dynamic_cast<ParamWidget*>(APP->event->draggedWidget);
                    if (pw != NULL && pw->paramQuantity == getParamQuantity(0))
                        recTouched = true;
                    else 
                        doRecord = false;
                }
                else {
                    doRecord = false;
                }
            }

            if (recMode == RECMODE_MOVE && !recTouched) {
                // check if param value has changed
                if (getValue() != recTouch)
                    recTouched = true;
                else
                    doRecord = false;
            }

            if (doRecord) {
                if (precisionCount == 0) {
                    // check if mouse button has been released
                    // NB: maybe unstable API
                    if (APP->event->draggedWidget == NULL) {
                        if (recMode == RECMODE_TOUCH) {     
                            stopRecording();
                        }
                        if (recMode == RECMODE_MOVE) {
                            stopRecording();
                            // trim unchanged values from the end
                            int i = seqLow + seqLength[seq] - 1;
                            if (i > seqLow) {
                                float l = data[i];
                                while (i > seqLow && l == data[i - 1]) i--;
                                seqLength[seq] = i - seqLow;
                            }
                        } 
                    }
                    
                    // are we still recording?
                    if (isRecording) {
                        data[dataPtr] = getValue();
                        seqLength[seq]++;
                        dataPtr++;
                        // stop recording when store is full
                        if (dataPtr == seqHigh) {
                            stopRecording();
                        }
                    }
                }
                precisionCount = (precisionCount + 1) % (int)pow(2, precision);
            }
        }
        else {
            // Move to previous sequence on button-press
            if (seqPTrigger.process(params[SEQP_PARAM].getValue())) {
                seqPrev();
            }

            // Move to next sequence on button-press
            if (seqNTrigger.process(params[SEQN_PARAM].getValue())) {
                seqNext();
            }

            // SEQCV-input
            if (inputs[SEQ_INPUT].isConnected()) {
                switch (seqCvMode) {
                    case 0:     // 0-10V
                        seqSet(round(rescale(inputs[SEQ_INPUT].getVoltage(), 0.f, 10.f, 0, seqCount - 1)));
                        break;
                    case 1:     // C4-G4
                        seqSet(round(clamp(inputs[SEQ_INPUT].getVoltage() * 12.f, 0.f, MAX_SEQ - 1.f)));
                        break;
                    case 2:     // Trigger
                        if (seqCvTrigger.process(inputs[SEQ_INPUT].getVoltage()))
                            seqNext();
                        break;
                }
            }

            // Reset ptr when button is pressed or input is triggered
            if (resetCvTrigger.process(params[RESET_PARAM].getValue() + inputs[RESET_INPUT].getVoltage())) {
                dataPtr = seqLow;
                playDir = 1;
                precisionCount = 0;
                valueFilters[0].reset();
            }

            // Toggle playing when button is pressed
            if (runTrigger.process(params[RUN_PARAM].getValue())) {
                isPlaying ^= true;
                precisionCount = 0;
            }

            // Set playing when input is high
            if (inputs[RUN_INPUT].isConnected()) {
                isPlaying = (inputs[RUN_INPUT].getVoltage() >= 1.f);
            }

            // If position-input is connected set the position directly, ignore playing
            if (inputs[PHASE_INPUT].isConnected()) {
                isPlaying = false;
                ParamQuantity *paramQuantity = getParamQuantity(0);
                if (paramQuantity != NULL) {
                    float v = clamp(inputs[PHASE_INPUT].getVoltage(), 0.f, 10.f);
                    dataPtr = floor(rescale(v, 0.f, 10.f, seqLow, seqLow + seqLength[seq] - 1));
                    v = data[dataPtr];
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
        }

		// Set channel lights infrequently
		if (lightDivider.process()) {
            lights[RUN_LIGHT].setBrightness(isPlaying);
            lights[RESET_LIGHT].setSmoothBrightness(resetCvTrigger.isHigh(), lightDivider.getDivision() * args.sampleTime);
            lights[REC_LIGHT].setBrightness(isRecording);

            for (int i = 0; i < 8; i++) {
                lights[SEQ_LIGHT + i].setBrightness((seq == i ? 0.7f : 0) + (seqCount >= i + 1 ? 0.3f : 0));
            }
        }

        MapModule::process(args);
    }

    inline float getValue() {
        float v;
        if (inputs[CV_INPUT].isConnected()) {
            v = rescale(inputs[CV_INPUT].getVoltage(), 0.f, 10.f, 0.f, 1.f);
        }
        else {
            ParamQuantity *paramQuantity = getParamQuantity(0);
            v = paramQuantity->getScaledValue();
        }
        return v;
    }

    void startRecording() {
        seqLength[seq] = 0;
        dataPtr = seqLow;
        precisionCount = 0;
        paramHandles[0].color = nvgRGB(0xff, 0x40, 0xff);
        recTouch = getValue();
        recTouched = false;
    }

    void stopRecording() {
        isRecording = false;
        dataPtr = seqLow;
        precisionCount = 0;        
        paramHandles[0].color = nvgRGB(0x40, 0xff, 0xff);
        valueFilters[0].reset();   
    }

    void seqNext() {
        seq = (seq + 1) % seqCount;
        seqUpdate();
    }

    void seqPrev() {
        seq = (seq - 1 + seqCount) % seqCount;
        seqUpdate();
    }

    void seqSet(int c) {
        if (c == seq) return;
        seq = clamp(c, 0, seqCount - 1);
        seqUpdate();
    }

    void seqResize(int c) {
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
            float last1 = 100.f, last2 = -100.f;
            for (int j = 0; j < seqLength[i]; j++) {
                if (last1 == last2) {
                    // 2 times same value -> compress!
                    int c = 0;
                    while (data[i * s + j] == last1 && j < seqLength[i]) { c++; j++; }
                    json_array_append(seqData1J, json_integer(c));
                    if (j < seqLength[i]) json_array_append(seqData1J, json_real(data[i * s + j]));                    
                    last1 = 100.f; last2 = -100.f;
                } 
                else {
                    json_array_append(seqData1J, json_real(data[i * s + j]));
                    last2 = last1;
                    last1 = data[i * s + j];
                }
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
        json_object_set_new(rootJ, "seqCvMode", json_integer(seqCvMode));
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
        json_t *seqCvModeJ = json_object_get(rootJ, "seqCvMode");
		if (seqCvModeJ) seqCvMode = json_integer_value(seqCvModeJ);
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
                float last1 = 100.f, last2 = -100.f;
                int c = 0;
                json_array_foreach(seqData1J, j, d) {
                    if (c > seqLength[i]) continue;
                    if (last1 == last2) {
                        // we've seen two same values -> decompress!
                        int v = json_integer_value(d);
                        for (int k = 0; k < v; k++) { data[i * s + c] = last1; c++; }
                        last1 = 100.f; last2 = -100.f;
                    }
                    else {
                        data[i * s + c] = json_real_value(d);
                        last2 = last1;
                        last1 = data[i * s + c];
                        c++;
                    }
                }
			}
		}
        seqUpdate();
	}   
};


struct ReMoveDisplay : TransparentWidget {
    const float maxX = 61.5f, maxY = 42.f;
	ReMove *module;
	//shared_ptr<Font> font;

	ReMoveDisplay() {
		//font = Font::load(assetPlugin(plugin, "res/DejaVuSansMono.ttf"));
	}
	
	void draw(NVGcontext *vg) override {
        if (!module) return;
        //if (module->isRecording) return;
		//nvgFontSize(vg, 12);
		//nvgFontFaceId(vg, font->handle);
		//nvgTextLetterSpacing(vg, -2);
		//nvgFillColor(vg, nvgRGBA(0xff, 0xff, 0xff, 0xff));	
		//nvgTextBox(vg, 5, 5, 120, module->fileDesc.c_str(), NULL);
		
		// Draw ref line
		nvgStrokeColor(vg, nvgRGBA(0xff, 0xb0, 0xf3, 0x20));
        nvgBeginPath(vg);
        nvgMoveTo(vg, 0, maxY / 2);
        nvgLineTo(vg, maxX, maxY / 2);
        nvgClosePath(vg);
        nvgStroke(vg);

        int seqPos = module->dataPtr - module->seqLow;
        int seqLength = module->seqLength[module->seq];
        if (seqLength < 2) return;
        
		// Draw play line
		nvgStrokeColor(vg, nvgRGBA(0xff, 0xb0, 0xf3, 0xb0));
        nvgStrokeWidth(vg, 0.7);
        nvgBeginPath(vg);
        nvgMoveTo(vg, seqPos * maxX / seqLength, 0 + 5.5);
        nvgLineTo(vg, seqPos * maxX / seqLength, maxY - 5.5);
        nvgClosePath(vg);
		nvgStroke(vg);
            
		// Draw automation-line
		nvgStrokeColor(vg, nvgRGBA(0xff, 0xd7, 0x14, 0xc0));
		nvgSave(vg);
		Rect b = Rect(Vec(0, 7), Vec(maxX, 56));
		nvgScissor(vg, b.pos.x, b.pos.y, b.size.x, b.size.y);
		nvgBeginPath(vg);
        int c = std::min(seqLength, 120);
		for (int i = 0; i < c; i++) {
            float x = (float)i / (c - 1);
            float y = module->data[module->seqLow + (int)floor(x * (seqLength - 1))] / 2.0 + 0.5;
			float px = b.pos.x + b.size.x * x;
			float py = b.pos.y + b.size.y * (1.01 - y);
            if (i == 0)
                nvgMoveTo(vg, px, py);
            else
                nvgLineTo(vg, px, py);
		}

		nvgLineCap(vg, NVG_ROUND);
		nvgMiterLimit(vg, 2.0);
		nvgStrokeWidth(vg, 1.1);
		nvgGlobalCompositeOperation(vg, NVG_LIGHTER);
		nvgStroke(vg);			
		nvgResetScissor(vg);
		nvgRestore(vg);	
	}
};



struct SeqCvModeMenuItem : MenuItem {
    struct SeqCvModItem : MenuItem {
        ReMove *module;
        int seqCvMode;

        void onAction(const event::Action &e) override {
            module->seqCvMode = seqCvMode;
        }

        void step() override {
            rightText = module->seqCvMode == seqCvMode ? "✔" : "";
            MenuItem::step();
        }
    };
    
    ReMove *module;
    Menu *createChildMenu() override {
        Menu *menu = new Menu;
        std::vector<std::string> names = {"0-10V", "C4-G4", "Trigger"};
        for (size_t i = 0; i < names.size(); i++) {
            menu->addChild(construct<SeqCvModItem>(&MenuItem::text, names[i], &SeqCvModItem::module, module, &SeqCvModItem::seqCvMode, i));
        }
        return menu;
    }
};


struct PrecisionMenuItem : MenuItem {
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
    
    ReMove *module;
    Menu *createChildMenu() override {
        Menu *menu = new Menu;
        std::vector<std::string> names = {"8th", "16th", "32nd", "64th", "128th", "256th", "512nd", "1024th", "2048th"};
        for (size_t i = 0; i < names.size(); i++) {
            menu->addChild(construct<PrecisionItem>(&MenuItem::text, names[i], &PrecisionItem::module, module, &PrecisionItem::precision, i + 3));
        }
        return menu;
    }
};

struct SeqCountMenuItem : MenuItem {
    struct SeqCountItem : MenuItem {
        ReMove *module;
        int seqCount;

        void onAction(const event::Action &e) override {
            module->seqResize(seqCount);
        }

        void step() override {
            rightText = (module->seqCount == seqCount) ? "✔" : "";
            MenuItem::step();
        }
    };
    
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

struct RecordModeMenuItem : MenuItem {
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
    
    ReMove *module;
    Menu *createChildMenu() override {
        Menu *menu = new Menu;
        menu->addChild(construct<RecordModeItem>(&MenuItem::text, "Touch", &RecordModeItem::module, module, &RecordModeItem::recMode, RECMODE_TOUCH));
        menu->addChild(construct<RecordModeItem>(&MenuItem::text, "Move", &RecordModeItem::module, module, &RecordModeItem::recMode, RECMODE_MOVE));
        menu->addChild(construct<RecordModeItem>(&MenuItem::text, "Manual", &RecordModeItem::module, module, &RecordModeItem::recMode, RECMODE_MANUAL));
        return menu;
    }
};

struct PlayModeMenuItem : MenuItem {
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

struct RecButton : SvgSwitch {
    RecButton() {
        momentary = true;
        box.size = Vec(40.f, 40.f);
        addFrame(APP->window->loadSvg(asset::plugin(pluginInstance, "res/RecButton.svg")));
    }
};

struct RecLight : RedLight {
	RecLight() {
		bgColor = nvgRGB(0x66, 0x66, 0x66);
		box.size = Vec(27.f, 27.f);
	}
};


struct ReMoveWidget : ModuleWidget {
    ReMoveWidget(ReMove *module) {	
        setModule(module);
        setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/ReMove.svg")));

        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        addChild(createLightCentered<TinyLight<GreenLight>>(Vec(14.1f, 107.9f), module, ReMove::SEQ_LIGHT + 0));
        addChild(createLightCentered<TinyLight<GreenLight>>(Vec(20.8f, 107.9f), module, ReMove::SEQ_LIGHT + 1));
        addChild(createLightCentered<TinyLight<GreenLight>>(Vec(27.5f, 107.9f), module, ReMove::SEQ_LIGHT + 2));
        addChild(createLightCentered<TinyLight<GreenLight>>(Vec(34.2f, 107.9f), module, ReMove::SEQ_LIGHT + 3));
        addChild(createLightCentered<TinyLight<GreenLight>>(Vec(40.9f, 107.9f), module, ReMove::SEQ_LIGHT + 4));
        addChild(createLightCentered<TinyLight<GreenLight>>(Vec(47.6f, 107.9f), module, ReMove::SEQ_LIGHT + 5));
        addChild(createLightCentered<TinyLight<GreenLight>>(Vec(54.3f, 107.9f), module, ReMove::SEQ_LIGHT + 6));
        addChild(createLightCentered<TinyLight<GreenLight>>(Vec(61.0f, 107.9f), module, ReMove::SEQ_LIGHT + 7));

        addInput(createInputCentered<PJ301MPort>(Vec(54.1f, 238.7f), module, ReMove::RUN_INPUT));
        addParam(createParamCentered<TL1105>(Vec(54.1f, 212.2f), module, ReMove::RUN_PARAM));
        addChild(createLightCentered<SmallLight<GreenLight>>(Vec(42.3f, 224.9f), module, ReMove::RUN_LIGHT));

        addInput(createInputCentered<PJ301MPort>(Vec(21.1f, 238.7f), module, ReMove::RESET_INPUT));
        addParam(createParamCentered<TL1105>(Vec(21.1f, 212.2f), module, ReMove::RESET_PARAM));
        addChild(createLightCentered<SmallLight<GreenLight>>(Vec(33.4f, 251.9f), module, ReMove::RESET_LIGHT));

        addInput(createInputCentered<PJ301MPort>(Vec(21.1f, 171.f), module, ReMove::PHASE_INPUT));

        addInput(createInputCentered<PJ301MPort>(Vec(21.1f, 336.3f), module, ReMove::CV_INPUT));      
        addOutput(createOutputCentered<PJ301MPort>(Vec(54.1f, 336.3f), module, ReMove::CV_OUTPUT));

        addParam(createParamCentered<RecButton>(Vec(37.6f, 284.3f), module, ReMove::REC_PARAM));
        addChild(createLightCentered<RecLight>(Vec(37.6f, 284.3f), module, ReMove::REC_LIGHT));

        addInput(createInputCentered<PJ301MPort>(Vec(54.1f, 171.f), module, ReMove::SEQ_INPUT));
        addParam(createParamCentered<TL1105>(Vec(21.1f, 132.4f), module, ReMove::SEQP_PARAM));
        addParam(createParamCentered<TL1105>(Vec(54.1f, 132.4), module, ReMove::SEQN_PARAM));

		MapModuleDisplay<1> *mapWidget = createWidget<MapModuleDisplay<1>>(Vec(6.8f, 36.4f));
		mapWidget->box.size = Vec(61.5f, 23.f);
		mapWidget->setModule(module);
		addChild(mapWidget);

       	ReMoveDisplay *display = new ReMoveDisplay();
		display->module = module;
		display->box.pos = Vec(6.8f, 62.f);
		display->box.size = Vec(61.5f, 50.f);
		addChild(display); 
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

        PrecisionMenuItem *precisionMenuItem = construct<PrecisionMenuItem>(&MenuItem::text, "Precision", &PrecisionMenuItem::module, module);
        precisionMenuItem->rightText = RIGHT_ARROW;
        menu->addChild(precisionMenuItem);

        SeqCountMenuItem *seqCountMenuItem = construct<SeqCountMenuItem>(&MenuItem::text, "No of sequences", &SeqCountMenuItem::module, module);
        seqCountMenuItem->rightText = RIGHT_ARROW;
        menu->addChild(seqCountMenuItem);

        RecordModeMenuItem *recordModeMenuItem = construct<RecordModeMenuItem>(&MenuItem::text, "Record Mode", &RecordModeMenuItem::module, module);
        recordModeMenuItem->rightText = RIGHT_ARROW;
        menu->addChild(recordModeMenuItem);

        PlayModeMenuItem *playModeMenuItem = construct<PlayModeMenuItem>(&MenuItem::text, "Play Mode", &PlayModeMenuItem::module, module);
        playModeMenuItem->rightText = RIGHT_ARROW;
        menu->addChild(playModeMenuItem);

        SeqCvModeMenuItem *seqCvModeMenuItem = construct<SeqCvModeMenuItem>(&MenuItem::text, "SEQ CV Mode", &SeqCvModeMenuItem::module, module);
        seqCvModeMenuItem->rightText = RIGHT_ARROW;
        menu->addChild(seqCvModeMenuItem);
    }
};


Model *modelReMoveLite = createModel<ReMove, ReMoveWidget>("ReMoveLite");