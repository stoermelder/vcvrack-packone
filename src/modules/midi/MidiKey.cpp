#include "../../plugin.hpp"
#include "../../vcv/api.hpp"
#include "../../components/MidiWidget.hpp"
#include "../../utils/keyboard.hpp"
#include "../../ui/ModuleSelectProcessor.hpp"
#include "../../ui/ViewportHelper.hpp"
#include "MidiTrackingProcessor.hpp"

namespace StoermelderPackOne {
namespace MidiKey {

#define ID_CTRL -4
#define ID_ALT -3
#define ID_SHIFT -2

template<int MAX_CHANNELS = 16>
struct MidiKeyModule : Module, MidiTrackingProcessorHandler {
	/** [Stored to JSON] */
	int panelTheme = 0;
	
	struct SlotData {
		/** [Stored to Json] */
		int key = -1;
		/** [Stored to Json] */
		int mods = 0;
		/** [Stored to Json] */
		int64_t moduleId = -1;

		bool active = false;
	};

	struct SlotVector {
		std::vector<SlotData> v{MAX_CHANNELS + 3};
		SlotData& operator[] (int index) {
			if (index < 0) { return v[index + 4]; }
			return v[index + 3];
		}
	};

	/** [Stored to Json] */
	SlotVector slot;

	/** Number of maps */
	int mapLen = 0;
	/** Channel ID of the learning session */
	int learningId;
	/** Whether the key has been set during the learning session */
	bool learnedKey;

	/** [Stored to JSON] */
	MidiTrackingProcessor<MAX_CHANNELS + 3> trackingProcessor;

	dsp::RingBuffer<std::tuple<event::HoverKey, int64_t>, 8> keyEventQueue;
	ModuleSelectProcessor moduleSelectProcessor;

	MidiKeyModule() {
		panelTheme = pluginSettings.panelThemeDefault;
		config(0, 0, 0, 0);
		
		ResetEvent re;
		onReset(re);

		trackingProcessor.handler = this;
		trackingProcessor.enableCc();
		trackingProcessor.enableNotes();
	}

	inline uint16_t getMapId(int id) {
		return uint16_t(id < 0 ? (id + 4) : (id + 3));
	}

	inline int getMapIdRev(uint16_t mapId) {
		return mapId >= 3 ? (mapId - 3) : (mapId - 4);
	}

	void onReset(const ResetEvent& e) override {
		learningId = -1;
		learnedKey = false;
		clearMaps();
		mapLen = 1;
		for (size_t i = 0; i < slot.v.size(); i++) {
			slot.v[i].key = -1;
			slot.v[i].mods = 0;
			slot.v[i].active = false;
		}
		trackingProcessor.disableMapLearn();
		trackingProcessor.clearMaps();
		trackingProcessor.getInput().reset();
		trackingProcessor.reset();
		Module::onReset(e);
	}

	void processBypass(const ProcessArgs &args) override {
		// Drain the queue while bypassed
		trackingProcessor.processBypass(args.frame);
		Module::processBypass(args);
	}

	void process(const ProcessArgs &args) override {
		trackingProcessor.process(args.frame);
	}

	// MidiTrackingProcessorHandler
	void processMapUpdate(MidiTrackingType type, uint16_t mapId, uint16_t value) override {
		switch (type) {
			case MidiTrackingType::NOTE:
			case MidiTrackingType::CC:
				processKey(getMapIdRev(mapId), value);
				break;
			default:
				break;
		}
	}

	// MidiTrackingProcessorHandler
	void processMapLearn(MidiTrackingType type, uint16_t mapId) override {
		commitLearn();
		updateMapLen();
	}

	void enableLearn(int id) {
		if (id == -1) {
			// Find next incomplete map
			while (++id < MAX_CHANNELS) {
				auto m = trackingProcessor.getMap(getMapId(id));
				if (m.type == MidiTrackingType::NONE && slot[id].key < 0)
					break;
			}
			if (id == MAX_CHANNELS) {
				return;
			}
		}

		if (id == mapLen) {
			disableLearn();
			return;
		}
		if (learningId != id) {
			learningId = id;
			learnedKey = false;
			trackingProcessor.enableMapLearn(getMapId(id));
		}
		return;
	}

	void disableLearn(int id = -1) {
		if (id == -1) {
			// Disable whatever learn session is currently active.
			// getMapId(-1) would collide with channel 0's map id, so use the
			// unconditional overload instead of the selective one.
			learningId = -1;
			trackingProcessor.disableMapLearn();
		}
		else if (learningId == id) {
			learningId = -1;
			trackingProcessor.disableMapLearn(getMapId(id));
		}
	}

	void commitLearn() {
		if (learningId == -1)
			return;
		if (trackingProcessor.getMapLearn())
			return;
		if (!learnedKey && learningId >= 0)
			return;
		// Reset learned state
		learnedKey = false;
		learningId = -1;
	}

	void clearMap(int id, bool midiOnly = false) {
		learningId = -1;
		trackingProcessor.clearMap(getMapId(id));
		// Clear the held state so a latched modifier (or a stale active flag on
		// a channel) does not leak into subsequent key events.
		slot[id].active = false;
		if (!midiOnly) {
			slot[id].key = -1;
			slot[id].mods = 0;
		}
		updateMapLen();
	}

	void clearMaps() {
		learningId = -1;
		// Clear all slots, including the three modifier rows (v[0..2]).
		for (size_t i = 0; i < slot.v.size(); i++) {
			slot.v[i].key = -1;
			slot.v[i].mods = 0;
			slot.v[i].active = false;
		}
		mapLen = 1;
		trackingProcessor.clearMaps();
	}

	void updateMapLen() {
		// Find last nonempty map
		int id;
		for (id = MAX_CHANNELS - 1; id >= 0; id--) {
			auto m = trackingProcessor.getMap(getMapId(id));
			if (m.type != MidiTrackingType::NONE || slot[id].key >= 0)
				break;
		}
		mapLen = id + 1;
		// Add an empty "Mapping..." slot
		if (mapLen < MAX_CHANNELS) {
			mapLen++;
		}
	}

	void learnKey(int key, int mods) {
		// No learn session: slot[-1] would alias slot[0] via the addressing
		// scheme, silently overwriting channel 0's binding.
		if (learningId < 0) return;
		slot[learningId].key = key;
		slot[learningId].mods = mods & (RACK_MOD_CTRL | GLFW_MOD_ALT | GLFW_MOD_SHIFT);
		learnedKey = true;
		commitLearn();
		updateMapLen();
	}

	void processKey(int id, uint8_t value) {
		switch (id) {
			case ID_CTRL:
				slot[ID_CTRL].active = value > 0;
				return;
			case ID_ALT:
				slot[ID_ALT].active = value > 0;
				return;
			case ID_SHIFT:
				slot[ID_SHIFT].active = value > 0;
				return;
			default: {
				// Skip duplicate events
				if ((value > 0 && slot[id].active) || (value == 0 && !slot[id].active))
					return;
				if (slot[id].key != -1) {
					event::HoverKey e;
					e.key = slot[id].key;
					e.scancode = glfwGetKeyScancode(e.key);
					const char* keyName = glfwGetKeyName(e.key, e.scancode);
					if (keyName) {
						e.keyName = keyName;
					}
					e.action = value > 0 ? GLFW_PRESS : GLFW_RELEASE;
					e.mods = 0;
					if (slot[ID_CTRL].active || (slot[id].mods & RACK_MOD_CTRL))
						e.mods = e.mods | RACK_MOD_CTRL;
					if (slot[ID_ALT].active || (slot[id].mods & GLFW_MOD_ALT))
						e.mods = e.mods | GLFW_MOD_ALT;
					if (slot[ID_SHIFT].active || (slot[id].mods & GLFW_MOD_SHIFT))
						e.mods = e.mods | GLFW_MOD_SHIFT;
					keyEventQueue.push(std::make_tuple(e, slot[id].moduleId));
				}
				slot[id].active = value > 0;
				return;
			}
		}
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));

		json_t* mapsJ = json_array();
		for (size_t i = 0; i < slot.v.size(); i++) {
			json_t* mapJ = json_object();
			json_object_set_new(mapJ, "key", json_integer(slot.v[i].key));
			json_object_set_new(mapJ, "mods", json_integer(slot.v[i].mods));
			json_object_set_new(mapJ, "moduleId", json_integer(slot.v[i].moduleId));
			json_array_append_new(mapsJ, mapJ);
		}
		json_object_set_new(rootJ, "maps", mapsJ);

		json_t* trackingProcessorJ = trackingProcessor.dataToJson();
		json_object_set_new(rootJ, "trackingProcessor", trackingProcessorJ);

		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		json_t* panelThemeJ = json_object_get(rootJ, "panelTheme");
		if (panelThemeJ) panelTheme = json_integer_value(panelThemeJ);

		clearMaps();
		json_t* mapsJ = json_object_get(rootJ, "maps");
		json_t* mapJ;
		size_t i;
		json_array_foreach(mapsJ, i, mapJ) {
			// A preset written by a build with more channels must not write past
			// the end of the slot vector.
			if (i >= slot.v.size()) break;
			json_t* keyJ = json_object_get(mapJ, "key");
			if (keyJ) slot.v[i].key = json_integer_value(keyJ);
			json_t* modsJ = json_object_get(mapJ, "mods");
			if (modsJ) slot.v[i].mods = json_integer_value(modsJ);
			json_t* moduleIdJ = json_object_get(mapJ, "moduleId");
			if (moduleIdJ) slot.v[i].moduleId = json_integer_value(moduleIdJ);
		}

		trackingProcessor.clearMaps();
		json_t* trackingProcessorJ = json_object_get(rootJ, "trackingProcessor");
		if (trackingProcessorJ) {
			trackingProcessor.dataFromJson(trackingProcessorJ);
		}
		else {
			// legacy format
			json_t* midiInputJ = json_object_get(rootJ, "midiInput");
			trackingProcessor.getInput().fromJson(midiInputJ);

			json_t* mapsJ = json_object_get(rootJ, "maps");
			json_t* mapJ;
			size_t j;
			json_array_foreach(mapsJ, j, mapJ) {
				if (j >= MAX_CHANNELS + 3) break;
				json_t* ccJ = json_object_get(mapJ, "cc");
				int cc = json_integer_value(ccJ);
				// cc/note index 128-element vectors in the tracking processor,
				// so out-of-range values from a corrupt preset must be dropped.
				if (cc >= 0 && cc < 128) {
					trackingProcessor.setMap(MidiTrackingType::CC, j, cc);
				}
				json_t* noteJ = json_object_get(mapJ, "note");
				int note = json_integer_value(noteJ);
				if (note >= 0 && note < 128) {
					trackingProcessor.setMap(MidiTrackingType::NOTE, j, note);
				}
			}
		}

		updateMapLen();
	}
};


template<typename MODULE = MidiKeyModule<>>
struct MidiKeyChoice : LedDisplayChoice {
	MODULE* module;
	int id;

	MidiKeyChoice() {
		box.size = mm2px(Vec(0, 7.5));
		textOffset = Vec(6.f, 14.7f);
		color = nvgRGB(0xf0, 0xf0, 0xf0);
	}

	void setModule(MODULE* module) {
		this->module = module;
	}

	void onButton(const event::Button& e) override {
		e.stopPropagating();
		if (!module) return;

		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			e.consume(this);
		}

		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_RIGHT) {
			e.consume(this);
			if (module->slot[id].key >= 0) {
				createContextMenu();
			} 
			else {
				module->clearMap(id);
			}
		}
	}

	void onSelect(const event::Select& e) override {
		if (!module) return;
		module->enableLearn(id);
	}

	void onDeselect(const event::Deselect& e) override {
		if (!module) return;
		module->disableLearn(id);
	}

	std::string getSlotPrefix() {
		static const char* noteNames[] = {
			" C", "C#", " D", "D#", " E", " F", "F#", " G", "G#", " A", "A#", " B"
		};
		if (module) {
			auto m = module->trackingProcessor.getMap(module->getMapId(id));
			if (m.type == MidiTrackingType::CC) {
				return string::f("cc%03d ", m.param);
			}
			else if (m.type == MidiTrackingType::NOTE) {
				int oct = m.param / 12 - 1;
				int semi = m.param % 12;
				return string::f("  %s%d ", noteNames[semi], oct);
			}
			else if (module->slot[id].key >= 0 || id < -1) {
				return "..... ";
			}
			return "      ";
		}
		else {
			// fake data for module browser
			return string::f(" %s2 ", noteNames[std::abs(id) % 12]);
		}
	}

	void step() override {
		if (!module) {
			// for module browser
			color.a = 0.5;
			std::string label = "";
			switch (id) {
				case ID_CTRL:
					label = RACK_MOD_CTRL_NAME; break;
				case ID_ALT:
					label = RACK_MOD_ALT_NAME; break;
				case ID_SHIFT:
					label = RACK_MOD_SHIFT_NAME; break;
				default:
					label = "> Unmapped"; break;
			}
			text = getSlotPrefix() + label;
			return;
		}

		// Set bgColor and selected state
		if (module->learningId == id) {
			bgColor = color;
			bgColor.a = 0.15;
			if (APP->event->getSelectedWidget() != this)
				APP->event->setSelectedWidget(this);
		} 
		else {
			bgColor = nvgRGBA(0, 0, 0, 0);
		}

		// Set text
		if ((module->slot[id].key >= 0 && module->learningId != id) || id < -1) {
			std::string label = "";
			switch (id) {
				case ID_CTRL:
					label = RACK_MOD_CTRL_NAME; break;
				case ID_ALT:
					label = RACK_MOD_ALT_NAME; break;
				case ID_SHIFT:
					label = RACK_MOD_SHIFT_NAME; break;
				default:
					label = "> ";
					if (module->slot[id].mods & RACK_MOD_CTRL) label += RACK_MOD_CTRL_NAME "+";
					if (module->slot[id].mods & GLFW_MOD_ALT) label += RACK_MOD_ALT_NAME "+";
					if (module->slot[id].mods & GLFW_MOD_SHIFT) label += RACK_MOD_SHIFT_NAME "+";
					label += keyName(module->slot[id].key).c_str();
					break;
			}
			text = getSlotPrefix() + label;
		} 
		else {
			if (module->learningId == id) {
				text = getSlotPrefix() + "Mapping...";
			} else {
				text = getSlotPrefix() + "Unmapped";
			}
		}

		// Set text color
		if (module->slot[id].key >= 0 || id < -1 || module->learningId == id) {
			color.a = 1.0;
		} 
		else {
			color.a = 0.5;
		}
	}

	void createContextMenu() {
		ui::Menu* menu = createMenu();
		menu->addChild(createMenuItem("Unmap", "", [=]() { module->clearMap(id); }));;
		menu->addChild(createMenuItem("Clear MIDI assignment", "", [=]() { module->clearMap(id, true); }));
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Modifiers"));
		menu->addChild(createCheckMenuItem(RACK_MOD_CTRL_NAME, "", [=]() { return module->slot[id].mods & RACK_MOD_CTRL; }, [=]() { module->slot[id].mods ^= RACK_MOD_CTRL; }));
		menu->addChild(createCheckMenuItem(RACK_MOD_ALT_NAME, "", [=]() { return module->slot[id].mods & GLFW_MOD_ALT; }, [=]() { module->slot[id].mods ^= GLFW_MOD_ALT; }));
		menu->addChild(createCheckMenuItem(RACK_MOD_SHIFT_NAME, "", [=]() { return module->slot[id].mods & GLFW_MOD_SHIFT; }, [=]() { module->slot[id].mods ^= GLFW_MOD_SHIFT; }));

		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Module"));
		if (module->slot[id].moduleId != -1) {
			ModuleWidget* mw = vcv::getModuleWidget(module->slot[id].moduleId);
			std::string name = mw ? string::f("%s %s", mw->model->plugin->brand.c_str(), mw->module->model->name.c_str()) : "<ERROR>";
			menu->addChild(createMenuLabel(name));
			if (mw) menu->addChild(createMenuItem("Show", "", [=]() { Rack::ViewportCenter{mw}; }));
			menu->addChild(createMenuItem("Clear", "", [=]() { module->slot[id].moduleId = -1; }));
		}
		menu->addChild(createMenuItem("Learn", "", [=]() {
			module->moduleSelectProcessor.setOwner(vcv::getModuleWidget(module->id));
			module->moduleSelectProcessor.startLearn([=](ModuleWidget* mw, Vec pos) {
				int64_t moduleId = -1;
				if (mw) moduleId = mw->module->getId();
				if (moduleId != -1 || module->slot[id].moduleId != -1) module->slot[id].moduleId = moduleId;
			});
		}));
	}

	void draw(const DrawArgs& args) override {
		if (module && module->slot[id].active) {
			nvgBeginPath(args.vg);
			nvgRect(args.vg, 0, 0, box.size.x, box.size.y);
			nvgFillColor(args.vg, color::mult(color::YELLOW, 0.2f));
			nvgGlobalCompositeOperation(args.vg, NVG_LIGHTER);
			nvgFill(args.vg);
		}

		LedDisplayChoice::draw(args);
		/*
		if (module && module->slot[id].active) {
			float x = 48.f;
			float y = box.size.y / 2.f;
			// Light
			nvgBeginPath(args.vg);
			nvgCircle(args.vg, x, y, 1.2f);
			nvgFillColor(args.vg, color::YELLOW);
			nvgFill(args.vg);
			// Halo
			nvgBeginPath(args.vg);
			nvgCircle(args.vg, x, y, 4.0f);
			NVGpaint paint;
			NVGcolor icol = color::mult(color, 0.6f);
			NVGcolor ocol = nvgRGB(0, 0, 0);
			paint = nvgRadialGradient(args.vg, x, y, 1.f, 4.f, icol, ocol);
			nvgFillPaint(args.vg, paint);
			nvgGlobalCompositeOperation(args.vg, NVG_LIGHTER);
			nvgFill(args.vg);
		}
		*/
	}
};

template<typename MODULE = MidiKeyModule<>>
struct MidiKeyModDisplay : LedDisplay {
	void setModule(MODULE* module) {
		Vec pos;
		for (int id = 0; id < 3; id++) {
			if (id > 0) {
				LedDisplaySeparator* separator = createWidget<LedDisplaySeparator>(pos);
				separator->box.size.x = box.size.x;
				addChild(separator);
			}

			MidiKeyChoice<MODULE>* choice = createWidget<MidiKeyChoice<MODULE>>(pos);
			choice->box.size.x = box.size.x;
			choice->id = id - 4;
			choice->setModule(module);
			addChild(choice);

			pos = choice->box.getBottomLeft();
		}
	}
};

template<int MAX_CHANNELS, typename MODULE = MidiKeyModule<>>
struct MidiKeyDisplay : LedDisplay {
	MODULE* module;
	ScrollWidget* scroll;
	MidiKeyChoice<MODULE>* choices[MAX_CHANNELS];
	LedDisplaySeparator* separators[MAX_CHANNELS];

	void step() override {
		if (module) {
			int mapLen = module->mapLen;
			for (int id = 1; id < MAX_CHANNELS; id++) {
				choices[id]->visible = (id < mapLen);
				separators[id]->visible = (id < mapLen);
			}
		}
		LedDisplay::step();
	}

	void setModule(MODULE* module) {
		this->module = module;

		scroll = new ScrollWidget;
		scroll->box.pos.y = 2.f;
		scroll->box.size.x = box.size.x;
		scroll->box.size.y = box.size.y - scroll->box.pos.y - 2.f;
		addChild(scroll);

		LedDisplaySeparator* separator = createWidget<LedDisplaySeparator>(scroll->box.pos);
		separator->box.size.x = box.size.x;
		addChild(separator);
		separators[0] = separator;

		Vec pos;
		for (int id = 0; id < MAX_CHANNELS; id++) {
			if (id > 0) {
				LedDisplaySeparator* separator = createWidget<LedDisplaySeparator>(pos);
				separator->box.size.x = box.size.x;
				scroll->container->addChild(separator);
				separators[id] = separator;
			}

			MidiKeyChoice<MODULE>* choice = createWidget<MidiKeyChoice<MODULE>>(pos);
			choice->box.size.x = box.size.x;
			choice->id = id;
			choice->setModule(module);
			scroll->container->addChild(choice);
			choices[id] = choice;

			pos = choice->box.getBottomLeft();
		}
	}
};


struct MidiKeyWidget : ThemedModuleWidget<MidiKeyModule<>> {
	MidiKeyWidget(MidiKeyModule<>* module)
		: ThemedModuleWidget<MidiKeyModule<16>>(module, "MidiKey") {
		setModule(module);

		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		MidiWidget<>* midiInputWidget = createWidget<MidiWidget<>>(Vec(0.0f, 36.4f));
		midiInputWidget->box.size = Vec(150.0f, 67.0f);
		midiInputWidget->setMidiPort(module ? &module->trackingProcessor.getInput() : NULL, "In");
		addChild(midiInputWidget);

		MidiKeyModDisplay<>* modWidget = createWidget<MidiKeyModDisplay<>>(Vec(0.0f, 107.4f));
		modWidget->box.size = Vec(150.0f, 67.0f);
		modWidget->setModule(module);
		addChild(modWidget);

		MidiKeyDisplay<16>* mapWidget = createWidget<MidiKeyDisplay<16>>(Vec(0.0f, 178.5f));
		mapWidget->box.size = Vec(150.0f, 164.7f);
		mapWidget->setModule(module);
		addChild(mapWidget);
	}

	void step() override {
		while (module && module->keyEventQueue.size() > 0) {
			std::tuple<event::HoverKey, int64_t> t = module->keyEventQueue.shift();
			event::HoverKey e = std::get<0>(t);
			int64_t moduleId = std::get<1>(t);
			
			if (moduleId != -1) {
				ModuleWidget* mw = vcv::getModuleWidget(moduleId);
				if (mw) mw->onHoverKey(e);
			}
			else {
				Vec pos = APP->scene->getMousePos();
				APP->event->handleKey(pos, e.key, e.scancode, e.action, e.mods);
			}
		}
		ThemedModuleWidget<MidiKeyModule<>>::step();
	}

	void onDeselect(const event::Deselect& e) override {
		ThemedModuleWidget<MidiKeyModule<>>::onDeselect(e);
		if (module) module->moduleSelectProcessor.processDeselect();
	}

	void onHoverKey(const event::HoverKey& e) override {
		if (module && module->learningId >= 0 && e.action == GLFW_PRESS) {
			int e_key = keyFix(e.key);
			std::string kn = keyName(e_key);
			if (!kn.empty()) {
				module->learnKey(e_key, e.mods);
				e.consume(this);
			}
		}
		Widget::onHoverKey(e);
	}
};

} // namespace MidiKey
} // namespace StoermelderPackOne

Model* modelMidiKey = createModel<StoermelderPackOne::MidiKey::MidiKeyModule<>, StoermelderPackOne::MidiKey::MidiKeyWidget>("MidiKey");