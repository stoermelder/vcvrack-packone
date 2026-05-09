#pragma once
#include "GlueModule.hpp"
#include "ModuleLabelWidget.hpp"
#include "CableLabelWidget.hpp"

namespace StoermelderPackOne {
namespace Glue {

// Forward declaration
struct LabelContainer;

// Port context menu extender for adding cable labels
struct PortWidgetContextExtender {
	Widget* lastSelectedWidget = NULL;
	GlueModule* module = NULL;
	LabelContainer* labelContainer = NULL;

	void step();
	void extendPortWidgetContextMenu(PortWidget* pw, Menu* menu);
};


struct LabelContainer : widget::Widget {
	GlueModule* module;
	std::list<ModuleLabel*> moduleLabelsToBeDeleted;
	std::list<CableLabel*> cableLabelsToBeDeleted;

	/** used when duplicating an existing label */
	ModuleLabel* moduleLabelTemplate = NULL;
	CableLabel* cableLabelTemplate = NULL;

	/** labels locked? */
	bool editMode = false;
	/** labels hidden? gets its value from the module's parameter */
	bool hideMode = false;
	/** learning a module for a new label? */
	bool learnMode = false;

	ModuleWidget* mw;
	
	/** Port context menu extender */
	PortWidgetContextExtender portExtender;

	/** Track cable tension for invalidating all caches when it changes */
	float lastCableTension = -1.f;

	void step() override;
	void draw(const DrawArgs& args) override;
	void drawLayer(const DrawArgs& args, int layer) override;

	ModuleLabelWidget* getModuleLabelWidget(ModuleLabel* l);
	ModuleLabelWidget* addModuleLabelWidget();
	void removeLabelWidget(ModuleLabel* l);

	CableLabelWidget* getCableLabelWidget(CableLabel* cl);
	CableLabelWidget* addCableLabelWidget(int64_t cableId, bool atInput);
	void removeCableLabelWidget(CableLabel* cl);

	void addLabelAtMousePos(Widget* w);
	void toggleLearnMode();
	void toggleEditMode();
	void toggleHideMode(bool doHide);

	void onHoverKey(const event::HoverKey& e) override;
};

} // namespace Glue
} // namespace StoermelderPackOne
