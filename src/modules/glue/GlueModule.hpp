#pragma once
#include "GlueTypes.hpp"
#include "../../utils/StripIdFixModule.hpp"

namespace StoermelderPackOne {
namespace Glue {

struct GlueModule : Module, StripIdFixModule {
	enum ParamIds {
		PARAM_UNLOCK,
		PARAM_ADD_LABEL,
		PARAM_OPACITY_PLUS,
		PARAM_OPACITY_MINUS,
		PARAM_HIDE,
		NUM_PARAMS
	};
	enum InputIds {
		NUM_INPUTS
	};
	enum OutputIds {
		NUM_OUTPUTS
	};
	enum LightIds {
		LIGHT_LEARN,
		LIGHT_LOCK,
		NUM_LIGHTS
	};

	/** [Stored to JSON] */
	int panelTheme = 0;

	/** [Stored to JSON] the list of labels */
	std::list<ModuleLabel*> moduleLabels;

	/** [Stored to JSON] the list of cable labels */
	std::list<CableLabel*> cableLabels;
	
	/** Transient list of cable labels requested for deletion */
	std::list<CableLabel*> cableLabelsToDelete;

	/** [Stored to JSON] default size for new labels */
	float defaultSize;
	/** [Stored to JSON] default width for new labels */
	float defaultWidth;
	/** [Stored to JSON] default angle for new labels */
	float defaultAngle;
	/** [Stored to JSON] default opacity for new labels */
	float defaultOpacity;
	/** [Stored to JSON] default color for new labels */
	NVGcolor defaultColor;
	/** [Stored to JSON] default font for new labels */
	int defaultFont;
	/** [Stored to JSON] */
	NVGcolor defaultFontColor;
	/** [Stored to JSON] */
	bool skewLabels;

	bool resetRequested = false;

	GlueModule();
	~GlueModule();

	void onReset() override;
	ModuleLabel* addModuleLabel();
	void removeModuleLabel(ModuleLabel* l);
	void clearLabels();
	CableLabel* addCableLabel();
	void removeCableLabel(CableLabel* cl);
	void clearCableLabels();

	json_t* dataToJson() override;
	json_t* moduleLabelToJson();
	json_t* cableLabelToJson();
	void dataFromJson(json_t* rootJ) override;
	void moduleLabelFromJson(json_t* labelsJ);
	void cableLabelFromJson(json_t* cableLabelsJ);
};

} // namespace Glue
} // namespace StoermelderPackOne
