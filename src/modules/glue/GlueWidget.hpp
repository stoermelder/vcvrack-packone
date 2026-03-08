#pragma once
#include "LabelContainer.hpp"
#include "../../components/Knobs.hpp"

namespace StoermelderPackOne {
namespace Glue {

// Template for undo/redo action to remove module labels
template <class WIDGET>
struct ModuleLabelRemoveAction : history::Action {
	WIDGET* widget = NULL;
	int64_t moduleId;
	ModuleLabel label;

	void undo() override;
	void redo() override;
};

struct LabelButton : TL1105 {
	LabelContainer* labelContainer;
	void onButton(const event::Button& e) override;
};

struct LockButton : TL1105 {
	LabelContainer* labelContainer;
	void onButton(const event::Button& e) override;
};

struct OpacityPlusButton : TL1105 {
	GlueModule* module;
	void onButton(const event::Button& e) override;
};

struct OpacityMinusButton : TL1105 {
	GlueModule* module;
	void onButton(const event::Button& e) override;
};

struct HideSwitch : CKSS {
	LabelContainer* labelContainer = NULL;
	void step() override;
};


struct GlueWidget : ThemedModuleWidget<GlueModule> {
	LabelContainer* labelContainer = NULL;

	template <class TParamWidget>
	TParamWidget* createParamCentered(math::Vec pos, engine::Module* module, int paramId) {
		TParamWidget* pw = rack::createParamCentered<TParamWidget>(pos, module, paramId);
		pw->labelContainer = labelContainer;
		return pw;
	}

	GlueWidget(GlueModule* module);
	~GlueWidget();

	void consolidate();
	void appendContextMenu(Menu* menu) override;
};

} // namespace Glue
} // namespace StoermelderPackOne
