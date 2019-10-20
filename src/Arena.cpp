#include "plugin.hpp"
#include <thread>

namespace Arena {

struct ArenaModule : Module {
	enum ParamIds {
		ENUMS(IN_X_PARAM, 8),
		ENUMS(IN_Y_PARAM, 8),
		ENUMS(IN_CTRL_PARAM, 8),
		ENUMS(IN_BCTRL_PARAM, 8),
		ENUMS(IN_PLUS_PARAM, 8),
		ENUMS(IN_MINUS_PARAM, 8),
		ALL_CTRL_PARAM,
		ALL_BCTRL_PARAM,
		ALL_PLUS_PARAM,
		ALL_MINUS_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		ENUMS(INPUT, 8),
		ENUMS(IN_X_INPUT, 8),
		ENUMS(IN_Y_INPUT, 8),
		ENUMS(CTRL_INPUT, 8),
		ENUMS(OUT_X_INPUT, 2),
		ENUMS(OUT_Y_INPUT, 2),
		ALL_CTRL_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		ENUMS(OUTPUT, 2),
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	ArenaModule() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
	}

	void process(const ProcessArgs &args) override {
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
	}
};


struct ArenaWidget : ModuleWidget {
	ArenaModule* module;

	ArenaWidget(ArenaModule* module) {
		setModule(module);
		this->module = module;
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/Arena.svg")));

		addChild(createWidget<MyBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<MyBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<MyBlackScrew>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<MyBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
	}

	void appendContextMenu(Menu* menu) override {
		struct ManualItem : MenuItem {
			void onAction(const event::Action &e) override {
				std::thread t(system::openBrowser, "https://github.com/stoermelder/vcvrack-packone/blob/v1/docs/Arena.md");
				t.detach();
			}
		};

		menu->addChild(construct<ManualItem>(&MenuItem::text, "Module Manual"));
		menu->addChild(new MenuSeparator());
	}
};

} // namespace Arena

Model* modelArena = createModel<Arena::ArenaModule, Arena::ArenaWidget>("Arena");