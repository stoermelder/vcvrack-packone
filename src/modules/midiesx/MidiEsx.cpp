#include "../../plugin.hpp"
#include "../../pluginhelpers.hpp"
#include "MidiEsx.hpp"

namespace StoermelderPackOne {
namespace MidiEsx {

struct MidiEsxProcessor {
	dsp::DoubleRingBuffer<float, 2048> bitQueue;
	bool locked = false;

	void bitEnqueue(const uint8_t* cmd, int len) {
		locked = true;
		if (int(bitQueue.capacity()) < len * 16) {
			return;
		}
		for (int i = 0; i < len; i++) {
			// Start-bit
			bitQueue.push(1);
			bitQueue.push(1);

			// Convert integer to bit-array
			// -> oversample with factor 3 = 24 bits
			bool buf[24];
			for (int j = 0; j < 8; j++) {
				bool b = ((1<<j) & cmd[i]) ? false : true;
				for (int k = 0; k < 3; k++) buf[j * 3 + k] = b;
			}

			// Resample datarate to 31250 bit/s assuming a samplerate of 48kHz
			// -> downsample 24 bits to 12 samples
			for (int j = 0; j < 12; j++) {
				int k = buf[j * 2] + buf[j * 2 + 1];
				bool bit = (k == 2) ? 1 : (k == 0) ? 0 : buf[j * 2 + 1];
				bitQueue.push(bit);
			}

			// Stop-bit
			bitQueue.push(0);
			bitQueue.push(0);
		}
		locked = false;
	}

	float nextBit() {
		if (locked) return 0.f;
		return bitQueue.size() > 0 ? bitQueue.shift() : 0.f;
	}
};

struct MidiEsxModule : Module, MidiEsxMessageHandler {
	enum ParamIds {
		NUM_PARAMS
	};
	enum InputIds {
		NUM_INPUTS
	};
	enum OutputIds {
		ENUMS(OUTPUT_ENC, 8),
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	int panelTheme = 0;

	/** [Stored to JSON] */
	int portGroupId = 0;

	MidiEsxProcessor port[8];
	bool portActive[8] = {false};

	MidiEsxModule() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		for (int i = 0; i < 8; i++) {
			configOutput(OUTPUT_ENC + i, string::f("Encoded MIDI %i", i + 1));
		}
		subscribe(portGroupId, this);
		onReset();
	}

	~MidiEsxModule() {
		unsubscribe(portGroupId, this);
	}
	
	void process(const ProcessArgs& args) override {
		if (args.sampleRate != 48000.f) return;

		for (int i = 0; i < 8; i++) {
			portActive[i] = outputs[OUTPUT_ENC + i].isConnected();
			outputs[OUTPUT_ENC + i].setVoltage(port[i].nextBit());
		}
	}

	// MidiEsxMessageHandler
	void onMessage(int portIndex, const rack::midi::Message& message) override {
		port[portIndex].bitEnqueue(message.bytes.data(), message.getSize());
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "portGroupId", json_integer(portGroupId));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		unsubscribe(portGroupId, this);
		portGroupId = json_integer_value(json_object_get(rootJ, "portGroupId"));
		subscribe(portGroupId, this);
	}

	void setportGroupId(int portGroupId) {
		unsubscribe(this->portGroupId, this);
		this->portGroupId = portGroupId;
		subscribe(this->portGroupId, this);
	}
};

struct MidiEsxWidget : ThemedModuleWidget<MidiEsxModule> {
	MidiEsxWidget(MidiEsxModule* module)
	: ThemedModuleWidget<MidiEsxModule>(module, "MidiEsx", "MidiEsx", true) {
		setModule(module);

		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addOutput(createOutputCentered<StoermelderPort>(Vec(22.5f, 56.5f + 0 * 33.f), module, MidiEsxModule::OUTPUT_ENC + 0));
		addOutput(createOutputCentered<StoermelderPort>(Vec(22.5f, 56.5f + 1 * 33.f), module, MidiEsxModule::OUTPUT_ENC + 1));
		addOutput(createOutputCentered<StoermelderPort>(Vec(22.5f, 56.5f + 2 * 33.f), module, MidiEsxModule::OUTPUT_ENC + 2));
		addOutput(createOutputCentered<StoermelderPort>(Vec(22.5f, 56.5f + 3 * 33.f), module, MidiEsxModule::OUTPUT_ENC + 3));
		addOutput(createOutputCentered<StoermelderPort>(Vec(22.5f, 56.5f + 4 * 33.f), module, MidiEsxModule::OUTPUT_ENC + 4));
		addOutput(createOutputCentered<StoermelderPort>(Vec(22.5f, 56.5f + 5 * 33.f), module, MidiEsxModule::OUTPUT_ENC + 5));
		addOutput(createOutputCentered<StoermelderPort>(Vec(22.5f, 56.5f + 6 * 33.f), module, MidiEsxModule::OUTPUT_ENC + 6));
		addOutput(createOutputCentered<StoermelderPort>(Vec(22.5f, 56.5f + 7 * 33.f), module, MidiEsxModule::OUTPUT_ENC + 7));
	}

	void appendContextMenu(Menu* menu) override {
		ThemedModuleWidget<MidiEsxModule>::appendContextMenu(menu);

		menu->addChild(new MenuSeparator);
		menu->addChild(createBoolMenuItem("Driver active", "",
			[=]() {
				return StoermelderPackOne::pluginSettings.midiEsxDriverEnabled;
			}, 
			[=](bool b) {
				StoermelderPackOne::pluginSettings.midiEsxDriverEnabled = b;
				StoermelderPackOne::pluginSettings.saveToJson();
				if (b) {
					StoermelderPackOne::MidiEsx::init();
				}
			}
		));

		for (int i = 0; i < 4; ++i) {
			menu->addChild(createCheckMenuItem(string::f("Port group %c", 65 + i), "", [
				this, i]() { return module->portGroupId == i; }, 
				[this, i]() { module->setportGroupId(i); }
			));
		}
	}
};

} // namespace MidiEsx
} // namespace StoermelderPackOne

Model* modelMidiEsx = createModel<StoermelderPackOne::MidiEsx::MidiEsxModule, StoermelderPackOne::MidiEsx::MidiEsxWidget>("MidiEsx");