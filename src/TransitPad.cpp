#include "plugin.hpp"
#include "components/XyScreenWidget.hpp"
#include "components/XySeqWidget.hpp"

namespace StoermelderPackOne {
namespace Transit {

template <uint8_t SNAPSHOTS>
struct TransitPadModule : Module, XyScreenModule<SNAPSHOTS>, XySeqModule<1> {
	enum ParamIds {
		ENUMS(SNAPSHOT_X_POS, SNAPSHOTS),
		ENUMS(SNAPSHOT_Y_POS, SNAPSHOTS),
		ENUMS(OUT_X_POS, 1),
		ENUMS(OUT_Y_POS, 1),
		NUM_PARAMS
	};
	enum InputIds {
		ENUMS(OUT_X_INPUT, 1),
		ENUMS(OUT_Y_INPUT, 1),
		ENUMS(OUT_SEQ_INPUT, 1),
		ENUMS(OUT_SEQ_PH_INPUT, 1),
		NUM_INPUTS
	};
	enum OutputIds {
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	typedef XyScreenModule<SNAPSHOTS> Sc;
	typedef XySeqModule<1> Seq;

	/** [Stored to JSON] */
	int panelTheme = 0;

	/** [Stored to JSON] */
	uint8_t snapshotsUsed;

	dsp::ClockDivider lightDivider;

	TransitPadModule() {
		panelTheme = pluginSettings.panelThemeDefault;
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		for (uint8_t i = 0; i < SNAPSHOTS; i++) {
			configParam<XyScreenParamQuantity>(SNAPSHOT_X_POS + i, 0.0f, 1.0f, 0.1f + float(i) * (0.8f / (SNAPSHOTS - 1)), string::f("Snapshot %i x-pos", i + 1));
			configParam<XyScreenParamQuantity>(SNAPSHOT_Y_POS + i, 0.0f, 1.0f, 0.1f, string::f("Snapshot %i y-pos", i + 1));
		}

		configInput(OUT_X_INPUT, "Output x-pos");
		configInput(OUT_Y_INPUT, "Output y-pos");
		configInput(OUT_SEQ_INPUT, "Output sequence select");
		configInput(OUT_SEQ_PH_INPUT, "Output sequence phase");
		configParam<XyScreenParamQuantity>(OUT_X_POS, 0.0f, 1.0f, 0.5f, "Output x-pos");
		configParam<XyScreenParamQuantity>(OUT_Y_POS, 0.0f, 1.0f, 0.9f, "Output y-pos");

		onReset();
	}
};


template <typename MODULE>
struct TransitPadSnapshotDragWidget : XyScreenDragWidget<MODULE> {
	typedef XyScreenDragWidget<MODULE> AW;

	TransitPadSnapshotDragWidget() {
		AW::color = color::WHITE;
		AW::type = 0;
	}

	void step() override {
		AW::circleA = AW::module->amount[AW::id];
		AW::step();
	}

	void drawLayer(const Widget::DrawArgs& args, int layer) override {
		if (layer == 1) {
			if (AW::id + 1 > AW::module->snapshotsUsed) return;

			if (AW::module->scIsSelected(AW::type, AW::id)) {
				// Draw outer circle and fill
				Vec c = Vec(AW::box.size.x / 2.f, AW::box.size.y / 2.f);
				Rect b = Rect(AW::box.pos.mult(-1), AW::parent->box.size);
				nvgSave(args.vg);
				nvgScissor(args.vg, b.pos.x, b.pos.y, b.size.x, b.size.y);
				float sizeX = std::max(0.f, (AW::parent->box.size.x - 2 * AW::radius) * AW::module->scGetRadiusFinal(AW::id) - AW::radius);
				float sizeY = std::max(0.f, (AW::parent->box.size.y - 2 * AW::radius) * AW::module->scGetRadiusFinal(AW::id) - AW::radius);
				nvgBeginPath(args.vg);
				nvgEllipse(args.vg, c.x, c.y, sizeX, sizeY);
				nvgGlobalCompositeOperation(args.vg, NVG_LIGHTER);
				nvgStrokeColor(args.vg, color::mult(AW::color, 0.7f));
				nvgStrokeWidth(args.vg, 0.6f);
				nvgStroke(args.vg);
				nvgFillColor(args.vg, color::mult(AW::color, 0.1f));
				nvgFill(args.vg);
				nvgResetScissor(args.vg);
				nvgRestore(args.vg);

				AW::textColor = nvgRGBA(0, 16, 90, 200);
			}
			else {
				AW::textColor = AW::color;
			}
		}
		AW::drawLayer(args, layer);
	}

	void onButton(const event::Button& e) override {
		if (AW::id + 1 > AW::module->inportsUsed) return;
		AW::onButton(e);
	}

 	std::string getItemName() override {
		return string::f("Snapshot %i", AW::id + 1);
	}
};


template <typename MODULE>
struct TransitPadScreenWidget : XyScreenWidget<MODULE> {
	typedef XyScreenWidget<MODULE> B;

	TransitPadScreenWidget(MODULE* module, int inParamIdX, int inParamIdY, int mixParamIdX, int mixParamIdY) : XyScreenWidget<MODULE>(module) {
		if (module) {
			for (uint8_t i = 0; i < module->numInports; i++) {
				TransitPadSnapshotDragWidget<MODULE>* w = new TransitPadSnapshotDragWidget<MODULE>;
				w->module = module;
				w->id = i;
				XyScreenWidget<MODULE>::addChild(w);
			}
		}
	}

	void appendContextMenu(Menu* menu) override {
		using StoermelderPackOne::Rack::createValuePtrMenuItem;
		menu->addChild(new MenuSeparator());
		menu->addChild(createSubmenuItem("Number of snapshots", string::f("%i", B::module->snapshotsUsed),
			[=](Menu* menu) {
				for (int i = 0; i < B::module->numInports; i++) {
					menu->addChild(createValuePtrMenuItem(string::f("%i", i + 1), &B::module->snapshotsUsed, i + 1));
				}
			}
		));
	}
};

} // namespace Transit
} // namespace StoermelderPackOne

//Model* modelTransitPad = createModel<StoermelderPackOne::Transit::TransitPadModule<>, StoermelderPackOne::Transit::TransitPadWidget<>>("TransitPad");