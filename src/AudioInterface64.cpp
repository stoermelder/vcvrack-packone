#include "plugin.hpp"
#include <audio.hpp>
#include <app.hpp>
#include <mutex>
#include <chrono>
#include <thread>
#include <mutex>
#include <condition_variable>


namespace AudioInterface64 {

template <int AUDIO_OUTPUTS, int AUDIO_INPUTS>
struct AudioInterfacePort : audio::Port {
	std::mutex engineMutex;
	std::condition_variable engineCv;
	std::mutex audioMutex;
	std::condition_variable audioCv;
	// Audio thread produces, engine thread consumes
	dsp::DoubleRingBuffer<dsp::Frame<AUDIO_INPUTS>, (1 << 15)> inputBuffer;
	// Audio thread consumes, engine thread produces
	dsp::DoubleRingBuffer<dsp::Frame<AUDIO_OUTPUTS>, (1 << 15)> outputBuffer;
	bool active = false;

	std::chrono::duration<int64_t, std::milli> timeout = std::chrono::milliseconds(100);

	~AudioInterfacePort() {
		// Close stream here before destructing AudioInterfacePort, so the mutexes are still valid when waiting to close.
		setDeviceId(-1, 0);
	}

	void processStream(const float* input, float* output, int frames) override {
		// Reactivate idle stream
		if (!active) {
			active = true;
			inputBuffer.clear();
			outputBuffer.clear();
		}

		if (numInputs > 0) {
			// TODO Do we need to wait on the input to be consumed here? Experimentally, it works fine if we don't.
			for (int i = 0; i < frames; i++) {
				if (inputBuffer.full())
					break;
				dsp::Frame<AUDIO_INPUTS> inputFrame;
				std::memset(&inputFrame, 0, sizeof(inputFrame));
				std::memcpy(&inputFrame, &input[numInputs * i], numInputs * sizeof(float));
				inputBuffer.push(inputFrame);
			}
		}

		if (numOutputs > 0) {
			std::unique_lock<std::mutex> lock(audioMutex);
			auto cond = [&] {
				return (outputBuffer.size() >= (size_t) frames);
			};
			if (audioCv.wait_for(lock, timeout, cond)) {
				// Consume audio block
				for (int i = 0; i < frames; i++) {
					dsp::Frame<AUDIO_OUTPUTS> f = outputBuffer.shift();
					for (int j = 0; j < numOutputs; j++) {
						output[numOutputs * i + j] = clamp(f.samples[j], -1.f, 1.f);
					}
				}
			}
			else {
				// Timed out, fill output with zeros
				std::memset(output, 0, frames * numOutputs * sizeof(float));
				// DEBUG("Audio Interface Port underflow");
			}
		}

		// Notify engine when finished processing
		engineCv.notify_one();
	}

	void onCloseStream() override {
		inputBuffer.clear();
		outputBuffer.clear();
	}

	void onChannelsChange() override {
	}
};


template <int AUDIO_OUTPUTS, int AUDIO_INPUTS>
struct AudioInterface : Module {
	enum ParamIds {
		NUM_PARAMS
	};
	enum InputIds {
		ENUMS(AUDIO_INPUT, AUDIO_INPUTS),
		NUM_INPUTS
	};
	enum OutputIds {
		ENUMS(AUDIO_OUTPUT, AUDIO_OUTPUTS),
		NUM_OUTPUTS
	};
	enum LightIds {
		ENUMS(INPUT_LIGHT, AUDIO_INPUTS / 2),
		ENUMS(OUTPUT_LIGHT, AUDIO_OUTPUTS / 2),
		NUM_LIGHTS
	};

	AudioInterfacePort<AUDIO_OUTPUTS, AUDIO_INPUTS> port;
	int lastSampleRate = 0;
	int lastNumOutputs = -1;
	int lastNumInputs = -1;

	dsp::SampleRateConverter<AUDIO_INPUTS> inputSrc;
	dsp::SampleRateConverter<AUDIO_OUTPUTS> outputSrc;

	// in rack's sample rate
	dsp::DoubleRingBuffer<dsp::Frame<AUDIO_INPUTS>, 16> inputBuffer;
	dsp::DoubleRingBuffer<dsp::Frame<AUDIO_OUTPUTS>, 16> outputBuffer;

	std::chrono::duration<int64_t, std::milli> timeout = std::chrono::milliseconds(200);

	dsp::ClockDivider lightDivider;

	AudioInterface() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		port.maxChannels = std::max(AUDIO_OUTPUTS, AUDIO_INPUTS);
		onSampleRateChange();
		lightDivider.setDivision(1024);
	}

	void process(const ProcessArgs& args) override {
		// Update SRC states
		inputSrc.setRates(port.sampleRate, args.sampleRate);
		outputSrc.setRates(args.sampleRate, port.sampleRate);

		inputSrc.setChannels(port.numInputs);
		outputSrc.setChannels(port.numOutputs);

		// Inputs: audio engine -> rack engine
		if (port.active && port.numInputs > 0) {
			// Wait until inputs are present
			// Give up after a timeout in case the audio device is being unresponsive.
			std::unique_lock<std::mutex> lock(port.engineMutex);
			auto cond = [&] {
				return (!port.inputBuffer.empty());
			};
			if (port.engineCv.wait_for(lock, timeout, cond)) {
				// Convert inputs
				int inLen = port.inputBuffer.size();
				int outLen = inputBuffer.capacity();
				inputSrc.process(port.inputBuffer.startData(), &inLen, inputBuffer.endData(), &outLen);
				port.inputBuffer.startIncr(inLen);
				inputBuffer.endIncr(outLen);
			}
			else {
				// Give up on pulling input
				port.active = false;
				// DEBUG("Audio Interface underflow");
			}
		}

		// Take input from buffer
		dsp::Frame<AUDIO_INPUTS> inputFrame;
		if (!inputBuffer.empty()) {
			inputFrame = inputBuffer.shift();
		}
		else {
			std::memset(&inputFrame, 0, sizeof(inputFrame));
		}
		for (int i = 0; i < port.numInputs; i++) {
			outputs[AUDIO_OUTPUT + i].setVoltage(10.f * inputFrame.samples[i]);
		}		
		if (lastNumInputs != port.numInputs) {
			lastNumInputs = port.numInputs;
			for (int i = port.numInputs; i < AUDIO_INPUTS; i++) {
				outputs[AUDIO_OUTPUT + i].setVoltage(0.f);
			}
		}
		
		// Outputs: rack engine -> audio engine
		if (port.active && port.numOutputs > 0) {
			// Get and push output SRC frame
			if (!outputBuffer.full()) {
				dsp::Frame<AUDIO_OUTPUTS> outputFrame;
				std::memset(&outputFrame, 0, sizeof(outputFrame));
				for (int i = 0; i < port.numOutputs; i++) {
					if (inputs[AUDIO_INPUT + i].isConnected()) {
						outputFrame.samples[i] = inputs[AUDIO_INPUT + i].getVoltageSum() / 10.f;
					}
				}
				outputBuffer.push(outputFrame);
			}

			if (outputBuffer.full()) {
				// Wait until enough outputs are consumed
				// Give up after a timeout in case the audio device is being unresponsive.
				auto cond = [&] {
					return (port.outputBuffer.size() < (size_t) port.blockSize);
				};
				if (!cond())
					APP->engine->yieldWorkers();
				std::unique_lock<std::mutex> lock(port.engineMutex);
				if (port.engineCv.wait_for(lock, timeout, cond)) {
					// Push converted output
					int inLen = outputBuffer.size();
					int outLen = port.outputBuffer.capacity();
					outputSrc.process(outputBuffer.startData(), &inLen, port.outputBuffer.endData(), &outLen);
					outputBuffer.startIncr(inLen);
					port.outputBuffer.endIncr(outLen);
				}
				else {
					// Give up on pushing output
					port.active = false;
					outputBuffer.clear();
					// DEBUG("Audio Interface underflow");
				}
			}

			// Notify audio thread that an output is potentially ready
			port.audioCv.notify_one();
		}

		// Set channel lights infrequently
		if (lightDivider.process()) {
			// Turn on light if at least one port is enabled in the nearby pair
			for (int i = 0; i < AUDIO_INPUTS / 2; i++)
				lights[INPUT_LIGHT + i].setBrightness(port.active && port.numOutputs >= 2 * i + 1);
			for (int i = 0; i < AUDIO_OUTPUTS / 2; i++)
				lights[OUTPUT_LIGHT + i].setBrightness(port.active && port.numInputs >= 2 * i + 1);
		}
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "audio", port.toJson());
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		json_t* audioJ = json_object_get(rootJ, "audio");
		port.fromJson(audioJ);
	}

	void onReset() override {
		port.setDeviceId(-1, 0);
	}
};


struct Audio64Widget : AudioWidget {
	void setAudioPort(audio::Port* port) {
		AudioWidget::setAudioPort(port);

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

struct AudioInterface64Widget : ModuleWidget {
	typedef AudioInterface<64, 64> TAudioInterface;

	AudioInterface64Widget(TAudioInterface* module) {
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/AudioInterface64.svg")));

		addChild(createWidget<MyBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<MyBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<MyBlackScrew>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<MyBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		for (int i = 0; i < 8; i++) {
			addInput(createInputCentered<StoermelderPort>(Vec(21.3f + i * 32.2f, 136.4f), module, TAudioInterface::AUDIO_INPUT + i * 8 + 0));
			addInput(createInputCentered<StoermelderPort>(Vec(21.3f + i * 32.2f, 163.7f), module, TAudioInterface::AUDIO_INPUT + i * 8 + 1));
			addInput(createInputCentered<StoermelderPort>(Vec(21.3f + i * 32.2f, 191.0f), module, TAudioInterface::AUDIO_INPUT + i * 8 + 2));
			addInput(createInputCentered<StoermelderPort>(Vec(21.3f + i * 32.2f, 218.6f), module, TAudioInterface::AUDIO_INPUT + i * 8 + 3));
			addInput(createInputCentered<StoermelderPort>(Vec(21.3f + i * 32.2f, 246.3f), module, TAudioInterface::AUDIO_INPUT + i * 8 + 4));
			addInput(createInputCentered<StoermelderPort>(Vec(21.3f + i * 32.2f, 273.6f), module, TAudioInterface::AUDIO_INPUT + i * 8 + 5));
			addInput(createInputCentered<StoermelderPort>(Vec(21.3f + i * 32.2f, 300.8f), module, TAudioInterface::AUDIO_INPUT + i * 8 + 6));
			addInput(createInputCentered<StoermelderPort>(Vec(21.3f + i * 32.2f, 328.1f), module, TAudioInterface::AUDIO_INPUT + i * 8 + 7));

			addOutput(createOutputCentered<StoermelderPort>(Vec(278.9f + i * 32.2f, 136.4f), module, TAudioInterface::AUDIO_OUTPUT + i * 8 + 0));
			addOutput(createOutputCentered<StoermelderPort>(Vec(278.9f + i * 32.2f, 163.7f), module, TAudioInterface::AUDIO_OUTPUT + i * 8 + 1));
			addOutput(createOutputCentered<StoermelderPort>(Vec(278.9f + i * 32.2f, 191.0f), module, TAudioInterface::AUDIO_OUTPUT + i * 8 + 2));
			addOutput(createOutputCentered<StoermelderPort>(Vec(278.9f + i * 32.2f, 218.6f), module, TAudioInterface::AUDIO_OUTPUT + i * 8 + 3));
			addOutput(createOutputCentered<StoermelderPort>(Vec(278.9f + i * 32.2f, 246.3f), module, TAudioInterface::AUDIO_OUTPUT + i * 8 + 4));
			addOutput(createOutputCentered<StoermelderPort>(Vec(278.9f + i * 32.2f, 273.6f), module, TAudioInterface::AUDIO_OUTPUT + i * 8 + 5));
			addOutput(createOutputCentered<StoermelderPort>(Vec(278.9f + i * 32.2f, 300.8f), module, TAudioInterface::AUDIO_OUTPUT + i * 8 + 6));
			addOutput(createOutputCentered<StoermelderPort>(Vec(278.9f + i * 32.2f, 328.1f), module, TAudioInterface::AUDIO_OUTPUT + i * 8 + 7));

			addChild(createLightCentered<TinyLight<GreenLight>>(Vec(21.3f + 9.8f + i * 32.2f, 150.1f), module, TAudioInterface::INPUT_LIGHT + i * 4 + 0));
			addChild(createLightCentered<TinyLight<GreenLight>>(Vec(21.3f + 9.8f + i * 32.2f, 205.0f), module, TAudioInterface::INPUT_LIGHT + i * 4 + 1));
			addChild(createLightCentered<TinyLight<GreenLight>>(Vec(21.3f + 9.8f + i * 32.2f, 259.9f), module, TAudioInterface::INPUT_LIGHT + i * 4 + 2));
			addChild(createLightCentered<TinyLight<GreenLight>>(Vec(21.3f + 9.8f + i * 32.2f, 314.5f), module, TAudioInterface::INPUT_LIGHT + i * 4 + 3));

			addChild(createLightCentered<TinyLight<GreenLight>>(Vec(278.9f - 9.8f + i * 32.2f, 146.0f), module, TAudioInterface::OUTPUT_LIGHT + i * 4 + 0));
			addChild(createLightCentered<TinyLight<GreenLight>>(Vec(278.9f - 9.8f + i * 32.2f, 205.0f), module, TAudioInterface::OUTPUT_LIGHT + i * 4 + 1));
			addChild(createLightCentered<TinyLight<GreenLight>>(Vec(278.9f - 9.8f + i * 32.2f, 259.9f), module, TAudioInterface::OUTPUT_LIGHT + i * 4 + 2));
			addChild(createLightCentered<TinyLight<GreenLight>>(Vec(278.9f - 9.8f + i * 32.2f, 314.5f), module, TAudioInterface::OUTPUT_LIGHT + i * 4 + 3));
		}

		Audio64Widget* audioWidget = createWidget<Audio64Widget>(Vec(132.5f, 36.0f));
		audioWidget->box.size = Vec(260.0f, 67.0f);
		audioWidget->setAudioPort(module ? &module->port : NULL);
		addChild(audioWidget);
	}
};

} // namespace AudioInterface64

Model* modelAudioInterface64 = createModel<AudioInterface64::AudioInterface<64, 64>, AudioInterface64::AudioInterface64Widget>("AudioInterface64");