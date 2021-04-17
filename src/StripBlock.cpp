#include "plugin.hpp"

namespace StoermelderPackOne {
namespace StripBlock {

struct StripBlockModule : Module {
	/** [Stored to JSON] */
	int panelTheme = 0;

	StripBlockModule() {
		panelTheme = pluginSettings.panelThemeDefault;
		config(0, 0, 0, 0);
		onReset();
	}

	json_t* dataToJson() override {
		json_t *rootJ = json_object();
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		panelTheme = json_integer_value(json_object_get(rootJ, "panelTheme"));
	}
}; // struct StripBlockModule


struct StripBlockWidget : ThemedModuleWidget<StripBlockModule> {
	StripBlockWidget(StripBlockModule* module)
		: ThemedModuleWidget<StripBlockModule>(module, "StripBlock") {
		setModule(module);

		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
	}
}; // struct StripBlock


} // namespace StripBlock
} // namespace StoermelderPackOne

Model* modelStripBlock = createModel<StoermelderPackOne::StripBlock::StripBlockModule, StoermelderPackOne::StripBlock::StripBlockWidget>("StripBlock");