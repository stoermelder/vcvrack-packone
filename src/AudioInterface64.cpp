#include "plugin.hpp"
#include <audio.hpp>
#include <context.hpp>
#include <mutex>
#include <chrono>
#include <mutex>
#include <condition_variable>

namespace StoermelderPackOne {
namespace AudioInterface64 {

template <int NUM_AUDIO_INPUTS, int NUM_AUDIO_OUTPUTS>
struct AudioInterfacePort : audio::Port {
	Module* module;

	dsp::DoubleRingBuffer<dsp::Frame<NUM_AUDIO_INPUTS>, 32768> engineInputBuffer;
	dsp::DoubleRingBuffer<dsp::Frame<NUM_AUDIO_OUTPUTS>, 32768> engineOutputBuffer;

	dsp::SampleRateConverter<NUM_AUDIO_INPUTS> inputSrc;
	dsp::SampleRateConverter<NUM_AUDIO_OUTPUTS> outputSrc;

	// Port variable caches
	int deviceNumInputs = 0;
	int deviceNumOutputs = 0;
	float deviceSampleRate = 0.f;
	int requestedEngineFrames = 0;

	AudioInterfacePort(Module* module) {
		this->module = module;
		maxOutputs = NUM_AUDIO_INPUTS;
		maxInputs = NUM_AUDIO_OUTPUTS;
		inputSrc.setQuality(6);
		outputSrc.setQuality(6);
	}

	void setMaster() {
		APP->engine->setMasterModule(module);
	}

	bool isMaster() {
		return APP->engine->getMasterModule() == module;
	}

	void processInput(const float* input, int inputStride, int frames) override {
		// DEBUG("%p: new device block ____________________________", this);
		// Claim master module if there is none
		if (!APP->engine->getMasterModule()) {
			setMaster();
		}
		bool isMasterCached = isMaster();

		// Set sample rate of engine if engine sample rate is "auto".
		if (isMasterCached) {
			APP->engine->setSuggestedSampleRate(deviceSampleRate);
		}

		float engineSampleRate = APP->engine->getSampleRate();
		float sampleRateRatio = engineSampleRate / deviceSampleRate;

		// DEBUG("%p: %d block, engineOutputBuffer still has %d", this, frames, (int) engineOutputBuffer.size());

		// Consider engine buffers "too full" if they contain a bit more than the audio device's number of frames, converted to engine sample rate.
		int maxEngineFrames = (int) std::ceil(frames * sampleRateRatio * 2.0) - 1;
		// If the engine output buffer is too full, clear it to keep latency low. No need to clear if master because it's always cleared below.
		if (!isMasterCached && (int) engineOutputBuffer.size() > maxEngineFrames) {
			engineOutputBuffer.clear();
			// DEBUG("%p: clearing engine output", this);
		}

		if (deviceNumInputs > 0) {
			// Always clear engine output if master
			if (isMasterCached) {
				engineOutputBuffer.clear();
			}
			// Set up sample rate converter
			outputSrc.setRates(deviceSampleRate, engineSampleRate);
			outputSrc.setChannels(deviceNumInputs);
			// Convert audio input -> engine output
			dsp::Frame<NUM_AUDIO_OUTPUTS> audioInputBuffer[frames];
			std::memset(audioInputBuffer, 0, sizeof(audioInputBuffer));
			for (int i = 0; i < frames; i++) {
				for (int j = 0; j < deviceNumInputs; j++) {
					float v = input[i * inputStride + j];
					audioInputBuffer[i].samples[j] = v;
				}
			}
			int audioInputFrames = frames;
			int outputFrames = engineOutputBuffer.capacity();
			outputSrc.process(audioInputBuffer, &audioInputFrames, engineOutputBuffer.endData(), &outputFrames);
			engineOutputBuffer.endIncr(outputFrames);
			// Request exactly as many frames as we have in the engine output buffer.
			requestedEngineFrames = engineOutputBuffer.size();
		}
		else {
			// Upper bound on number of frames so that `audioOutputFrames >= frames` when processOutput() is called.
			requestedEngineFrames = std::max((int) std::ceil(frames * sampleRateRatio) - (int) engineInputBuffer.size(), 0);
		}
	}

	void processBuffer(const float* input, int inputStride, float* output, int outputStride, int frames) override {
		// Step engine
		if (isMaster() && requestedEngineFrames > 0) {
			// DEBUG("%p: %d block, stepping %d", this, frames, requestedEngineFrames);
			APP->engine->stepBlock(requestedEngineFrames);
		}
	}

	void processOutput(float* output, int outputStride, int frames) override {
		// bool isMasterCached = isMaster();
		float engineSampleRate = APP->engine->getSampleRate();
		float sampleRateRatio = engineSampleRate / deviceSampleRate;

		if (deviceNumOutputs > 0) {
			// Set up sample rate converter
			inputSrc.setRates(engineSampleRate, deviceSampleRate);
			inputSrc.setChannels(deviceNumOutputs);
			// Convert engine input -> audio output
			dsp::Frame<NUM_AUDIO_OUTPUTS> audioOutputBuffer[frames];
			int inputFrames = engineInputBuffer.size();
			int audioOutputFrames = frames;
			inputSrc.process(engineInputBuffer.startData(), &inputFrames, audioOutputBuffer, &audioOutputFrames);
			engineInputBuffer.startIncr(inputFrames);
			// Copy the audio output buffer
			for (int i = 0; i < audioOutputFrames; i++) {
				for (int j = 0; j < deviceNumOutputs; j++) {
					float v = audioOutputBuffer[i].samples[j];
					v = clamp(v, -1.f, 1.f);
					output[i * outputStride + j] = v;
				}
			}
			// Fill the rest of the audio output buffer with zeros
			for (int i = audioOutputFrames; i < frames; i++) {
				for (int j = 0; j < deviceNumOutputs; j++) {
					output[i * outputStride + j] = 0.f;
				}
			}
		}

		// DEBUG("%p: %d block, engineInputBuffer left %d", this, frames, (int) engineInputBuffer.size());

		// If the engine input buffer is too full, clear it to keep latency low.
		int maxEngineFrames = (int) std::ceil(frames * sampleRateRatio * 2.0) - 1;
		if ((int) engineInputBuffer.size() > maxEngineFrames) {
			engineInputBuffer.clear();
			// DEBUG("%p: clearing engine input", this);
		}

		// DEBUG("%p %s:\tframes %d requestedEngineFrames %d\toutputBuffer %d engineInputBuffer %d\t", this, isMasterCached ? "master" : "secondary", frames, requestedEngineFrames, engineOutputBuffer.size(), engineInputBuffer.size());
	}

	void onStartStream() override {
		deviceNumInputs = std::min(getNumInputs(), NUM_AUDIO_OUTPUTS);
		deviceNumOutputs = std::min(getNumOutputs(), NUM_AUDIO_INPUTS);
		deviceSampleRate = getSampleRate();
		engineInputBuffer.clear();
		engineOutputBuffer.clear();
		// DEBUG("onStartStream %d %d %f", deviceNumInputs, deviceNumOutputs, deviceSampleRate);
	}

	void onStopStream() override {
		deviceNumInputs = 0;
		deviceNumOutputs = 0;
		deviceSampleRate = 0.f;
		engineInputBuffer.clear();
		engineOutputBuffer.clear();
		// DEBUG("onStopStream");
	}
};


template <int NUM_AUDIO_INPUTS, int NUM_AUDIO_OUTPUTS>
struct AudioInterface : Module {
	static constexpr int NUM_INPUT_LIGHTS = (NUM_AUDIO_INPUTS > 2) ? (NUM_AUDIO_INPUTS / 2) : 0;
	static constexpr int NUM_OUTPUT_LIGHTS = (NUM_AUDIO_OUTPUTS > 2) ? (NUM_AUDIO_OUTPUTS / 2) : 0;

	enum ParamIds {
		ENUMS(GAIN_PARAM, NUM_AUDIO_INPUTS == 2),
		NUM_PARAMS
	};
	enum InputIds {
		ENUMS(AUDIO_INPUTS, NUM_AUDIO_INPUTS),
		NUM_INPUTS
	};
	enum OutputIds {
		ENUMS(AUDIO_OUTPUTS, NUM_AUDIO_OUTPUTS),
		NUM_OUTPUTS
	};
	enum LightIds {
		ENUMS(INPUT_LIGHTS, NUM_INPUT_LIGHTS * 2),
		ENUMS(OUTPUT_LIGHTS, NUM_OUTPUT_LIGHTS * 2),
		ENUMS(VU_LIGHTS, (NUM_AUDIO_INPUTS == 2) ? (2 * 6) : 0),
		NUM_LIGHTS
	};

	AudioInterfacePort<NUM_AUDIO_INPUTS, NUM_AUDIO_OUTPUTS> port;

	dsp::RCFilter dcFilters[NUM_AUDIO_INPUTS];
	bool dcFilterEnabled = false;

	dsp::ClockDivider lightDivider;
	// For each pair of inputs/outputs
	float inputClipTimers[(NUM_AUDIO_INPUTS > 0) ? NUM_INPUT_LIGHTS : 0] = {};
	float outputClipTimers[(NUM_AUDIO_INPUTS > 0) ? NUM_OUTPUT_LIGHTS : 0] = {};
	dsp::VuMeter2 vuMeter[(NUM_AUDIO_INPUTS == 2) ? 2 : 0];

	/** [Stored to JSON] */
	int panelTheme = 0;

	AudioInterface() : port(this) {
		panelTheme = pluginSettings.panelThemeDefault;
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		if (NUM_AUDIO_INPUTS == 2)
			configParam(GAIN_PARAM, 0.f, 2.f, 1.f, "Level", " dB", -10, 40);
		for (int i = 0; i < NUM_AUDIO_INPUTS; i++)
			configInput(AUDIO_INPUTS + i, string::f("To \"device output %d\"", i + 1));
		for (int i = 0; i < NUM_AUDIO_OUTPUTS; i++)
			configOutput(AUDIO_OUTPUTS + i, string::f("From \"device input %d\"", i + 1));
		for (int i = 0; i < NUM_INPUT_LIGHTS; i++)
			configLight(INPUT_LIGHTS + 2 * i, string::f("Device output %d/%d status", 2 * i + 1, 2 * i + 2));
		for (int i = 0; i < NUM_OUTPUT_LIGHTS; i++)
			configLight(OUTPUT_LIGHTS + 2 * i, string::f("Device input %d/%d status", 2 * i + 1, 2 * i + 2));

		lightDivider.setDivision(512);

		float sampleTime = APP->engine->getSampleTime();
		for (int i = 0; i < NUM_AUDIO_INPUTS; i++) {
			dcFilters[i].setCutoffFreq(10.f * sampleTime);
		}

		onReset();
	}

	void onReset() override {
		port.setDriverId(-1);

		if (NUM_AUDIO_INPUTS == 2)
			dcFilterEnabled = true;
		else
			dcFilterEnabled = false;
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		port.engineInputBuffer.clear();
		port.engineOutputBuffer.clear();

		for (int i = 0; i < NUM_AUDIO_INPUTS; i++) {
			dcFilters[i].setCutoffFreq(10.f * e.sampleTime);
		}
	}

	void process(const ProcessArgs& args) override {
		const float clipTime = 0.25f;

		// Push inputs to buffer
		if (port.deviceNumOutputs > 0) {
			dsp::Frame<NUM_AUDIO_INPUTS> inputFrame = {};
			for (int i = 0; i < port.deviceNumOutputs; i++) {
				// Get input
				float v = 0.f;
				if (inputs[AUDIO_INPUTS + i].isConnected())
					v = inputs[AUDIO_INPUTS + i].getVoltageSum() / 10.f;
				// Normalize right input to left on Audio-2
				else if (i == 1 && NUM_AUDIO_INPUTS == 2)
					v = inputFrame.samples[0];

				// Apply DC filter
				if (dcFilterEnabled) {
					dcFilters[i].process(v);
					v = dcFilters[i].highpass();
				}

				// Detect clipping
				if (NUM_AUDIO_INPUTS > 2) {
					if (std::fabs(v) >= 1.f)
						inputClipTimers[i / 2] = clipTime;
				}
				inputFrame.samples[i] = v;
			}

			// Audio-2: Apply gain from knob
			if (NUM_AUDIO_INPUTS == 2) {
				float gain = std::pow(params[GAIN_PARAM].getValue(), 2.f);
				for (int i = 0; i < NUM_AUDIO_INPUTS; i++) {
					inputFrame.samples[i] *= gain;
				}
			}

			if (!port.engineInputBuffer.full()) {
				port.engineInputBuffer.push(inputFrame);
			}

			// Audio-2: VU meter process
			if (NUM_AUDIO_INPUTS == 2) {
				for (int i = 0; i < NUM_AUDIO_INPUTS; i++) {
					vuMeter[i].process(args.sampleTime, inputFrame.samples[i]);
				}
			}
		}
		else {
			// Audio-2: Clear VU meter
			if (NUM_AUDIO_INPUTS == 2) {
				for (int i = 0; i < NUM_AUDIO_INPUTS; i++) {
					vuMeter[i].reset();
				}
			}
		}

		// Pull outputs from buffer
		if (!port.engineOutputBuffer.empty()) {
			dsp::Frame<NUM_AUDIO_OUTPUTS> outputFrame = port.engineOutputBuffer.shift();
			for (int i = 0; i < NUM_AUDIO_OUTPUTS; i++) {
				float v = outputFrame.samples[i];
				outputs[AUDIO_OUTPUTS + i].setVoltage(10.f * v);

				// Detect clipping
				if (NUM_AUDIO_OUTPUTS > 2) {
					if (std::fabs(v) >= 1.f)
						outputClipTimers[i / 2] = clipTime;
				}
			}
		}
		else {
			// Zero outputs
			for (int i = 0; i < NUM_AUDIO_OUTPUTS; i++) {
				outputs[AUDIO_OUTPUTS + i].setVoltage(0.f);
			}
		}

		// Lights
		if (lightDivider.process()) {
			float lightTime = args.sampleTime * lightDivider.getDivision();
			// Audio-2: VU meter
			if (NUM_AUDIO_INPUTS == 2) {
				for (int i = 0; i < NUM_AUDIO_INPUTS; i++) {
					lights[VU_LIGHTS + i * 6 + 0].setBrightness(vuMeter[i].getBrightness(0, 0));
					lights[VU_LIGHTS + i * 6 + 1].setBrightness(vuMeter[i].getBrightness(-3, 0));
					lights[VU_LIGHTS + i * 6 + 2].setBrightness(vuMeter[i].getBrightness(-6, -3));
					lights[VU_LIGHTS + i * 6 + 3].setBrightness(vuMeter[i].getBrightness(-12, -6));
					lights[VU_LIGHTS + i * 6 + 4].setBrightness(vuMeter[i].getBrightness(-24, -12));
					lights[VU_LIGHTS + i * 6 + 5].setBrightness(vuMeter[i].getBrightness(-36, -24));
				}
			}
			// Audio-8 and Audio-16: pair state lights
			else {
				// Turn on light if at least one port is enabled in the nearby pair.
				for (int i = 0; i < NUM_AUDIO_INPUTS / 2; i++) {
					bool active = port.deviceNumOutputs >= 2 * i + 1;
					bool clip = inputClipTimers[i] > 0.f;
					if (clip)
						inputClipTimers[i] -= lightTime;
					lights[INPUT_LIGHTS + i * 2 + 0].setBrightness(active && !clip);
					lights[INPUT_LIGHTS + i * 2 + 1].setBrightness(active && clip);
				}
				for (int i = 0; i < NUM_AUDIO_OUTPUTS / 2; i++) {
					bool active = port.deviceNumInputs >= 2 * i + 1;
					bool clip = outputClipTimers[i] > 0.f;
					if (clip)
						outputClipTimers[i] -= lightTime;
					lights[OUTPUT_LIGHTS + i * 2 + 0].setBrightness(active & !clip);
					lights[OUTPUT_LIGHTS + i * 2 + 1].setBrightness(active & clip);
				}
			}
		}
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));
		json_object_set_new(rootJ, "audio", port.toJson());
		json_object_set_new(rootJ, "dcFilter", json_boolean(dcFilterEnabled));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		json_t* audioJ = json_object_get(rootJ, "audio");
		panelTheme = json_integer_value(json_object_get(rootJ, "panelTheme"));
		port.fromJson(audioJ);

		json_t* dcFilterJ = json_object_get(rootJ, "dcFilter");
		if (dcFilterJ)
			dcFilterEnabled = json_boolean_value(dcFilterJ);
	}

	/** Must be called when the Engine mutex is unlocked.
	*/
	void setMaster() {
		APP->engine->setMasterModule(this);
	}

	bool isMaster() {
		return APP->engine->getMasterModule() == this;
	}
};


struct Audio64Widget : AudioDisplay {
	void setAudioPort(audio::Port* port) {
		AudioDisplay::setAudioPort(port);

		driverChoice->textOffset = Vec(6.f, 14.7f);
		driverChoice->box.size = mm2px(Vec(driverChoice->box.size.x, 7.5f));
		driverChoice->color = nvgRGB(0xf0, 0xf0, 0xf0);

		driverSeparator->box.pos = driverChoice->box.getBottomLeft();

		deviceChoice->textOffset = Vec(6.f, 14.7f);
		deviceChoice->box.size = mm2px(Vec(deviceChoice->box.size.x, 7.5f));
		deviceChoice->box.pos = driverChoice->box.getBottomLeft();
		deviceChoice->color = nvgRGB(0xf0, 0xf0, 0xf0);

		deviceSeparator->box.pos = deviceChoice->box.getBottomLeft();

		sampleRateChoice->textOffset = Vec(6.f, 14.7f);
		sampleRateChoice->box.size = mm2px(Vec(sampleRateChoice->box.size.x, 7.5f));
		sampleRateChoice->box.pos = deviceChoice->box.getBottomLeft();
		sampleRateChoice->color = nvgRGB(0xf0, 0xf0, 0xf0);

		sampleRateSeparator->box.pos.y = sampleRateChoice->box.pos.y;

		bufferSizeChoice->textOffset = Vec(6.f, 14.7f);
		bufferSizeChoice->box.size.y = sampleRateChoice->box.size.y;
		bufferSizeChoice->box.pos.y = sampleRateChoice->box.pos.y;
		bufferSizeChoice->color = nvgRGB(0xf0, 0xf0, 0xf0);
	}
};

struct AudioInterface64Widget : ThemedModuleWidget<AudioInterface<64, 64>> {
	typedef AudioInterface<64, 64> TAudioInterface;

	AudioInterface64Widget(TAudioInterface* module)
		: ThemedModuleWidget<TAudioInterface>(module, "AudioInterface64") {
		setModule(module);

		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		for (int i = 0; i < 8; i++) {
			addInput(createInputCentered<StoermelderPort>(Vec(21.3f + i * 32.2f, 136.4f), module, TAudioInterface::AUDIO_INPUTS + i * 8 + 0));
			addInput(createInputCentered<StoermelderPort>(Vec(21.3f + i * 32.2f, 163.7f), module, TAudioInterface::AUDIO_INPUTS + i * 8 + 1));
			addInput(createInputCentered<StoermelderPort>(Vec(21.3f + i * 32.2f, 191.0f), module, TAudioInterface::AUDIO_INPUTS + i * 8 + 2));
			addInput(createInputCentered<StoermelderPort>(Vec(21.3f + i * 32.2f, 218.6f), module, TAudioInterface::AUDIO_INPUTS + i * 8 + 3));
			addInput(createInputCentered<StoermelderPort>(Vec(21.3f + i * 32.2f, 246.3f), module, TAudioInterface::AUDIO_INPUTS + i * 8 + 4));
			addInput(createInputCentered<StoermelderPort>(Vec(21.3f + i * 32.2f, 273.6f), module, TAudioInterface::AUDIO_INPUTS + i * 8 + 5));
			addInput(createInputCentered<StoermelderPort>(Vec(21.3f + i * 32.2f, 300.8f), module, TAudioInterface::AUDIO_INPUTS + i * 8 + 6));
			addInput(createInputCentered<StoermelderPort>(Vec(21.3f + i * 32.2f, 328.1f), module, TAudioInterface::AUDIO_INPUTS + i * 8 + 7));

			addOutput(createOutputCentered<StoermelderPort>(Vec(278.9f + i * 32.2f, 136.4f), module, TAudioInterface::AUDIO_OUTPUTS + i * 8 + 0));
			addOutput(createOutputCentered<StoermelderPort>(Vec(278.9f + i * 32.2f, 163.7f), module, TAudioInterface::AUDIO_OUTPUTS + i * 8 + 1));
			addOutput(createOutputCentered<StoermelderPort>(Vec(278.9f + i * 32.2f, 191.0f), module, TAudioInterface::AUDIO_OUTPUTS + i * 8 + 2));
			addOutput(createOutputCentered<StoermelderPort>(Vec(278.9f + i * 32.2f, 218.6f), module, TAudioInterface::AUDIO_OUTPUTS + i * 8 + 3));
			addOutput(createOutputCentered<StoermelderPort>(Vec(278.9f + i * 32.2f, 246.3f), module, TAudioInterface::AUDIO_OUTPUTS + i * 8 + 4));
			addOutput(createOutputCentered<StoermelderPort>(Vec(278.9f + i * 32.2f, 273.6f), module, TAudioInterface::AUDIO_OUTPUTS + i * 8 + 5));
			addOutput(createOutputCentered<StoermelderPort>(Vec(278.9f + i * 32.2f, 300.8f), module, TAudioInterface::AUDIO_OUTPUTS + i * 8 + 6));
			addOutput(createOutputCentered<StoermelderPort>(Vec(278.9f + i * 32.2f, 328.1f), module, TAudioInterface::AUDIO_OUTPUTS + i * 8 + 7));

			addChild(createLightCentered<TinyLight<GreenLight>>(Vec(21.3f + 9.8f + i * 32.2f, 150.1f), module, TAudioInterface::INPUT_LIGHTS + i * 4 + 0));
			addChild(createLightCentered<TinyLight<GreenLight>>(Vec(21.3f + 9.8f + i * 32.2f, 205.0f), module, TAudioInterface::INPUT_LIGHTS + i * 4 + 1));
			addChild(createLightCentered<TinyLight<GreenLight>>(Vec(21.3f + 9.8f + i * 32.2f, 259.9f), module, TAudioInterface::INPUT_LIGHTS + i * 4 + 2));
			addChild(createLightCentered<TinyLight<GreenLight>>(Vec(21.3f + 9.8f + i * 32.2f, 314.5f), module, TAudioInterface::INPUT_LIGHTS + i * 4 + 3));

			addChild(createLightCentered<TinyLight<GreenLight>>(Vec(278.9f - 9.8f + i * 32.2f, 146.0f), module, TAudioInterface::OUTPUT_LIGHTS + i * 4 + 0));
			addChild(createLightCentered<TinyLight<GreenLight>>(Vec(278.9f - 9.8f + i * 32.2f, 205.0f), module, TAudioInterface::OUTPUT_LIGHTS + i * 4 + 1));
			addChild(createLightCentered<TinyLight<GreenLight>>(Vec(278.9f - 9.8f + i * 32.2f, 259.9f), module, TAudioInterface::OUTPUT_LIGHTS + i * 4 + 2));
			addChild(createLightCentered<TinyLight<GreenLight>>(Vec(278.9f - 9.8f + i * 32.2f, 314.5f), module, TAudioInterface::OUTPUT_LIGHTS + i * 4 + 3));
		}

		Audio64Widget* audioWidget = createWidget<Audio64Widget>(Vec(132.5f, 36.0f));
		audioWidget->box.size = Vec(260.0f, 67.0f);
		audioWidget->setAudioPort(module ? &module->port : NULL);
		addChild(audioWidget);
	}

	void appendContextMenu(Menu* menu) override {
		TAudioInterface* module = dynamic_cast<TAudioInterface*>(this->module);

		menu->addChild(new MenuSeparator);

		menu->addChild(createCheckMenuItem("Master audio module", "",
			[=]() {return module->isMaster();},
			[=]() {module->setMaster();}
		));

		menu->addChild(createBoolPtrMenuItem("DC blocker", "", &module->dcFilterEnabled));
	}
};

} // namespace AudioInterface64
} // namespace StoermelderPackOne

Model* modelAudioInterface64 = createModel<StoermelderPackOne::AudioInterface64::AudioInterface<64, 64>, StoermelderPackOne::AudioInterface64::AudioInterface64Widget>("AudioInterface64");
