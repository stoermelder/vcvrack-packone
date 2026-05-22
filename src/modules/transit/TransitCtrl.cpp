#include "../../plugin.hpp"
#include "../../components/Knobs.hpp"
#include "TransitBase.hpp"

namespace StoermelderPackOne {
namespace Transit {

/**
 * TransitCtrl — expander for the Transit module
 *
 * How it works
 * ============
 *
 * Placement
 * ---------
 * TransitCtrl must be placed immediately to the right of Transit, before any
 * TransitEx expanders:  [Transit] [TransitCtrl] [TransitEx] ...
 * Transit detects this in its process() loop (when expandersChanged) and pushes
 * its own TransitCtrlSender pointer into TransitCtrl via TransitCtrlReceiver::setTransitCtrl().
 * When the modules are separated, TransitCtrl::onExpanderChange() clears all proxy
 * targets so the knobs become inert.
 *
 * Parameter proxy (ProxyParamQuantity)
 * -------------------------------------
 * Each knob on TransitCtrl owns a ProxyParamQuantity. Its min/max/default and all
 * display/label/unit methods are delegated to the corresponding mapped parameter's
 * ParamQuantity (accessed via TransitCtrlSender::getCtrlParamQuantity), so the knob
 * behaves identically to the mapped parameter from the UI's perspective (correct
 * drag range, tooltip text, context-menu value entry, etc.).
 * getValue/setValue are left as the default ParamQuantity implementations: the local
 * Param is self-contained storage whose value lives in the target's value range.
 *
 * Mapping
 * -------
 * mapping[k] stores which Transit ctrl param index knob k controls (-1 = unmapped).
 * The user selects this per-knob via the knob's context menu; it is serialized to JSON.
 * reverseMap[t] is the inverse: the knob index whose mapping equals t, or -1 if none.
 * Both tables are kept in sync by setMapping() and rebuilt from scratch in dataFromJson().
 * ProxyParamQuantity::handleIndex mirrors mapping[k] and is what getTargetPQ() uses.
 *
 * TransitCtrl → Transit (user or CV input changes a knob)
 * --------------------------------------------------------
 * TransitCtrlModule::process() compares each params[i].value against lastParamValues[i].
 * Any mismatch — whether from a knob drag (ParamQuantity::setValue) or a direct
 * Param::setValue call by an external CV mapper — is treated as a "control event"
 * and pushed into Transit's dsp::RingBuffer via TransitCtrlSender::pushCtrlChange,
 * using handleIndex as the Transit-side param index.
 * Transit drains that queue at the top of its own process() and writes the value to the
 * target parameter.  The queue is one-directional; Transit never calls pushCtrlChange
 * back, so there is no feedback loop.
 *
 * Transit → TransitCtrl (Transit applies a preset fade or phase interpolation)
 * ----------------------------------------------------------------------------
 * Whenever Transit writes a value to a mapped parameter it also calls
 * TransitCtrlReceiver::setCtrlParamValue(transitIndex, value).  That method looks up
 * the knob via reverseMap[transitIndex] and updates both params[PARAM+k].value and
 * lastParamValues[k] atomically, so process() sees no delta and does not re-queue the
 * change.  This keeps the knobs in sync with ongoing transitions without oscillation.
 *
 * External target changes → TransitCtrl (periodic polling)
 * ---------------------------------------------------------
 * A dsp::ClockDivider fires every 256 samples. On each tick, process() reads the live
 * raw value of every mapped target parameter via tpq->getParam()->getValue() and calls
 * setCtrlParamValue if it differs from lastParamValues[k].  This catches changes made
 * directly to the original parameter by any source other than Transit or TransitCtrl
 * (e.g. a MIDI mapper writing to the target param directly).
 */


/** Proxy ParamQuantity for a TransitCtrl knob. The local param stores the control value
 *  in the target's range; range, display, and label are all delegated to the target PQ.
 *  getValue/setValue are intentionally left as the default ParamQuantity implementations
 *  so that the local param is self-contained storage. Changes are detected in process()
 *  and forwarded to Transit via pushCtrlChange(), avoiding any write-back oscillation. */
struct ProxyParamQuantity : ParamQuantity {
	TransitCtrlSender* transitCtrl = nullptr;
	int handleIndex = 0;

	ParamQuantity* getTargetPQ() {
		if (!transitCtrl || handleIndex < 0) return nullptr;
		return transitCtrl->getCtrlParamQuantity(handleIndex);
	}

	float getMinValue() override {
		ParamQuantity* tpq = getTargetPQ();
		return tpq ? tpq->getMinValue() : ParamQuantity::getMinValue();
	}

	float getMaxValue() override {
		ParamQuantity* tpq = getTargetPQ();
		return tpq ? tpq->getMaxValue() : ParamQuantity::getMaxValue();
	}

	float getDefaultValue() override {
		ParamQuantity* tpq = getTargetPQ();
		return tpq ? tpq->getDefaultValue() : ParamQuantity::getDefaultValue();
	}

	float getDisplayValue() override {
		ParamQuantity* tpq = getTargetPQ();
		return tpq ? tpq->getDisplayValue() : ParamQuantity::getDisplayValue();
	}

	void setDisplayValue(float displayValue) override {
		ParamQuantity* tpq = getTargetPQ();
		if (tpq) tpq->setDisplayValue(displayValue);
		else ParamQuantity::setDisplayValue(displayValue);
	}

	int getDisplayPrecision() override {
		ParamQuantity* tpq = getTargetPQ();
		return tpq ? tpq->getDisplayPrecision() : ParamQuantity::getDisplayPrecision();
	}

	std::string getDisplayValueString() override {
		ParamQuantity* tpq = getTargetPQ();
		return tpq ? tpq->getDisplayValueString() : ParamQuantity::getDisplayValueString();
	}

	void setDisplayValueString(std::string s) override {
		ParamQuantity* tpq = getTargetPQ();
		if (tpq) tpq->setDisplayValueString(s);
		else ParamQuantity::setDisplayValueString(s);
	}

	std::string getLabel() override {
		ParamQuantity* tpq = getTargetPQ();
		return tpq ? tpq->getLabel() : ParamQuantity::getLabel();
	}

	std::string getUnit() override {
		ParamQuantity* tpq = getTargetPQ();
		return tpq ? tpq->getUnit() : ParamQuantity::getUnit();
	}

	std::string getString() override {
		ParamQuantity* tpq = getTargetPQ();
		return tpq ? tpq->getString() : ParamQuantity::getString();
	}

	std::string getDescription() override {
		ParamQuantity* tpq = getTargetPQ();
		return tpq ? tpq->getDescription() : ParamQuantity::getDescription();
	}
};


template <int NUM_CTRL>
struct TransitCtrlModule : Module, TransitCtrlReceiver {
	enum ParamIds {
		ENUMS(PARAM, NUM_CTRL),
		NUM_PARAMS
	};
	enum InputIds {
		NUM_INPUTS
	};
	enum OutputIds {
		OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	/** [Stored to JSON] */
	int panelTheme = 0;
	/** [Stored to JSON] mapping[k] = Transit ctrl param index for knob k, or -1 for unmapped */
	int mapping[NUM_CTRL];
	/** reverseMap[t] = knob index whose mapping[k] == t, or -1 if none */
	int reverseMap[NUM_CTRL];

	ProxyParamQuantity* ppqs[NUM_CTRL] = {};
	float lastParamValues[NUM_CTRL];
	dsp::ClockDivider targetSyncDivider;

	TransitCtrlModule() {
		panelTheme = pluginSettings.panelThemeDefault;
		Module::config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);

		for (int i = 0; i < NUM_CTRL; i++) {
			ppqs[i] = Module::configParam<ProxyParamQuantity>(PARAM + i, 0.f, 1.f, 0.5f, string::f("Ctrl %d", i + 1));
			ppqs[i]->handleIndex = i;
		}
		targetSyncDivider.setDivision(256);

		Module::ResetEvent re;
		onReset(re);
	}

	void onReset(const ResetEvent& e) override {
		for (int i = 0; i < NUM_CTRL; i++) {
			mapping[i] = -1;
			reverseMap[i] = -1;
			lastParamValues[i] = -1.f;
		}
		Module::onReset(e);
	}

	void onExpanderChange(const Module::ExpanderChangeEvent& e) override {
		// Clear proxy targets on any expander change; Transit re-injects its pointer
		// via setTransitCtrl() in the next process() tick if still connected.
		setTransitCtrl(nullptr);
		notifyExpanderListeners("Transit");
	}

	void process(const Module::ProcessArgs& args) override {
		if (targetSyncDivider.process()) {
			// Periodically sync knob display to the live target parameter value so that
			// external changes (not made via Transit or TransitCtrl) are reflected here.
			for (int i = 0; i < NUM_CTRL; i++) {
				if (!ppqs[i]->transitCtrl) continue;
				ParamQuantity* tpq = ppqs[i]->getTargetPQ();
				if (!tpq) continue;
				float targetV = tpq->getParam()->getValue();
				if (targetV != lastParamValues[i]) {
					params[PARAM + i].setValue(targetV);
					lastParamValues[i] = targetV;
				}
			}
			for (int i = 0; i < NUM_CTRL; i++) {
				float v = params[PARAM + i].getValue();
				if (v == lastParamValues[i]) continue;
				lastParamValues[i] = v;
				if (ppqs[i]->transitCtrl && ppqs[i]->handleIndex >= 0)
					ppqs[i]->transitCtrl->pushCtrlChange(ppqs[i]->handleIndex, v);
			}
		}
	}

	void setTransitCtrl(TransitCtrlSender* ctrl) override {
		for (int i = 0; i < NUM_CTRL; i++) {
			ppqs[i]->transitCtrl = ctrl;
			ppqs[i]->handleIndex = mapping[i];
			if (ctrl && mapping[i] >= 0) {
				ParamQuantity* tpq = ctrl->getCtrlParamQuantity(mapping[i]);
				if (tpq) setCtrlParamValue(mapping[i], tpq->getValue());
			}
		}
	}

	/** Called from the GUI thread to remap knob knobIndex to a different Transit ctrl param. */
	void setMapping(int knobIndex, int targetIndex) {
		int oldTarget = mapping[knobIndex];
		if (oldTarget >= 0 && reverseMap[oldTarget] == knobIndex)
			reverseMap[oldTarget] = -1;

		mapping[knobIndex] = targetIndex;
		ppqs[knobIndex]->handleIndex = targetIndex;

		if (targetIndex >= 0)
			reverseMap[targetIndex] = knobIndex;

		if (ppqs[knobIndex]->transitCtrl && targetIndex >= 0) {
			ParamQuantity* tpq = ppqs[knobIndex]->getTargetPQ();
			if (tpq) setCtrlParamValue(targetIndex, tpq->getValue());
		}
	}

	/** Transit calls this with its own ctrl param index; O(1) via reverseMap. */
	void setCtrlParamValue(int transitIndex, float value) override {
		if (transitIndex < 0 || transitIndex >= NUM_CTRL) return;
		int k = reverseMap[transitIndex];
		if (k < 0) return;
		params[PARAM + k].setValue(value);
		lastParamValues[k] = value;
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));
		json_t* mappingJ = json_array();
		for (int i = 0; i < NUM_CTRL; i++)
			json_array_append_new(mappingJ, json_integer(mapping[i]));
		json_object_set_new(rootJ, "mapping", mappingJ);
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		panelTheme = json_integer_value(json_object_get(rootJ, "panelTheme"));
		json_t* mappingJ = json_object_get(rootJ, "mapping");
		if (mappingJ) {
			for (int i = 0; i < NUM_CTRL; i++) reverseMap[i] = -1;
			for (int i = 0; i < NUM_CTRL && i < (int)json_array_size(mappingJ); i++) {
				mapping[i] = (int)json_integer_value(json_array_get(mappingJ, i));
				ppqs[i]->handleIndex = mapping[i];
				if (mapping[i] >= 0 && mapping[i] < NUM_CTRL)
					reverseMap[mapping[i]] = i;
			}
		}
	}
};


template <int NUM_CTRL>
struct TransitCtrlKnob : StoermelderSmallKnob {
	TransitCtrlModule<NUM_CTRL>* module;
	int knobIndex;

	void appendContextMenu(ui::Menu* menu) override {
		StoermelderSmallKnob::appendContextMenu(menu);
		if (!module) return;

		struct MappingItem : MenuItem {
			TransitCtrlModule<NUM_CTRL>* module;
			int knobIndex;
			int targetIndex;
			void onAction(const event::Action& e) override {
				module->setMapping(knobIndex, targetIndex);
			}
			void step() override {
				rightText = CHECKMARK(module->mapping[knobIndex] == targetIndex);
				MenuItem::step();
			}
		};

		menu->addChild(new MenuSeparator);
		ProxyParamQuantity* ppq = module->ppqs[knobIndex];
		if (ppq->transitCtrl) {
			menu->addChild(createSubmenuItem("Connect to TRANSIT parameter", "", [this, ppq](Menu* menu) {
				auto* unmapped = construct<MappingItem>(&MenuItem::text, "Unconnected",
					&MappingItem::module, module, &MappingItem::knobIndex, knobIndex, &MappingItem::targetIndex, -1);
				menu->addChild(unmapped);

				int count = ppq->transitCtrl->getCtrlParamCount();
				for (int i = 0; i < count; i++) {
					ParamQuantity* tpq = ppq->transitCtrl->getCtrlParamQuantity(i);                
					std::string label = tpq ? string::f("%s %s", tpq->module->model->name, tpq->getLabel().c_str()) : string::f("Param %d", i + 1);
					auto* item = construct<MappingItem>(&MenuItem::text, label,
						&MappingItem::module, module, &MappingItem::knobIndex, knobIndex, &MappingItem::targetIndex, i);
					menu->addChild(item);
				}
			}));
		}
		else {
			menu->addChild(createMenuLabel("(Connect TRANSIT first)"));
		}
	}
};

struct TransitCtrlWidget : ThemedModuleWidget<TransitCtrlModule<16>> {
	TransitCtrlWidget(TransitCtrlModule<16>* module)
		: ThemedModuleWidget<TransitCtrlModule<16>>(module, "TransitCtrl") {
		this->setModule(module);

		this->addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		this->addChild(createWidget<StoermelderBlackScrew>(Vec(this->box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		auto addCtrlKnob = [&](Vec pos, int paramId, int knobIndex) {
			TransitCtrlKnob<16>* knob = createParamCentered<TransitCtrlKnob<16>>(pos, module, paramId);
			knob->module = module;
			knob->knobIndex = knobIndex;
			this->addParam(knob);
		};

		// 8 rows x 2 columns, left column = knobs 0-7, right column = knobs 8-15
		addCtrlKnob(Vec(20.2f,  48.8f), TransitCtrlModule<16>::PARAM + 0,  0);
		addCtrlKnob(Vec(54.8f,  48.8f), TransitCtrlModule<16>::PARAM + 8,  8);
		addCtrlKnob(Vec(20.2f,  85.1f), TransitCtrlModule<16>::PARAM + 1,  1);
		addCtrlKnob(Vec(54.8f,  85.1f), TransitCtrlModule<16>::PARAM + 9,  9);
		addCtrlKnob(Vec(20.2f, 121.5f), TransitCtrlModule<16>::PARAM + 2,  2);
		addCtrlKnob(Vec(54.8f, 121.5f), TransitCtrlModule<16>::PARAM + 10, 10);
		addCtrlKnob(Vec(20.2f, 157.8f), TransitCtrlModule<16>::PARAM + 3,  3);
		addCtrlKnob(Vec(54.8f, 157.8f), TransitCtrlModule<16>::PARAM + 11, 11);
		addCtrlKnob(Vec(20.2f, 194.1f), TransitCtrlModule<16>::PARAM + 4,  4);
		addCtrlKnob(Vec(54.8f, 194.1f), TransitCtrlModule<16>::PARAM + 12, 12);
		addCtrlKnob(Vec(20.2f, 230.5f), TransitCtrlModule<16>::PARAM + 5,  5);
		addCtrlKnob(Vec(54.8f, 230.5f), TransitCtrlModule<16>::PARAM + 13, 13);
		addCtrlKnob(Vec(20.2f, 266.8f), TransitCtrlModule<16>::PARAM + 6,  6);
		addCtrlKnob(Vec(54.8f, 266.8f), TransitCtrlModule<16>::PARAM + 14, 14);
		addCtrlKnob(Vec(20.2f, 303.1f), TransitCtrlModule<16>::PARAM + 7,  7);
		addCtrlKnob(Vec(54.8f, 303.1f), TransitCtrlModule<16>::PARAM + 15, 15);
	}
};

} // namespace Transit
} // namespace StoermelderPackOne

Model* modelTransitCtrl = createModel<StoermelderPackOne::Transit::TransitCtrlModule<16>, StoermelderPackOne::Transit::TransitCtrlWidget>("TransitCtrl");
