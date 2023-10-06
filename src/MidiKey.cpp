#include "plugin.hpp"
#include "components/MidiWidget.hpp"
#include "ui/keyboard.hpp"
#include "ui/ModuleSelectProcessor.hpp"
#include "ui/ViewportHelper.hpp"

namespace StoermelderPackOne {
namespace MidiKey {

#define ID_CTRL -4
#define ID_ALT -3
#define ID_SHIFT -2

template<int MAX_CHANNELS = 16>
struct MidiKeyModule : Module {
	/** [Stored to JSON] */
	int panelTheme = 0;
	/** [Stored to Json] */
	midi::InputQueue midiInput;

	struct SlotData {
		/** [Stored to Json] */
		int key = -1;
		/** [Stored to Json] */
		int mods = 0;
		/** [Stored to Json] */
		int cc = -1;
		/** [Stored to Json] */
		int note = -1;
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
	int mapCc[128];
	int mapNote[128];

	/** Number of maps */
	int mapLen = 0;
	/** Channel ID of the learning session */
	int learningId;
	/** Whether the CC has been set during the learning session */
	bool learnedCc;
	/** Whether the note has been set during the learning session */
	bool learnedNote;
	/** Whether the key has been set during the learning session */
	bool learnedKey;

	dsp::RingBuffer<std::tuple<event::HoverKey, int64_t>, 8> keyEventQueue;
	ModuleSelectProcessor moduleSelectProcessor;

	MidiKeyModule() {
		panelTheme = pluginSettings.panelThemeDefault;
		config(0, 0, 0, 0);
		onReset();
	}

	void onReset() override {
		learningId = -1;
		learnedCc = false;
		learnedNote = false;
		learnedKey = false;
		clearMaps();
		mapLen = 1;
		for (size_t i = 0; i < slot.v.size(); i++) {
			slot.v[i].cc = -1;
			slot.v[i].note = -1;
			slot.v[i].key = -1;
			slot.v[i].mods = 0;
		}
		for (int i = 0; i < 128; i++) {
			mapCc[i] = -1;
			mapNote[i] = -1;
		}
		midiInput.reset();
	}

	void process(const ProcessArgs &args) override {
		midi::Message msg;
		while (midiInput.tryPop(&msg, args.frame)) {
			midiProcessMessage(msg);
		}
	}

	void midiProcessMessage(midi::Message msg) {
		switch (msg.getStatus()) {
			// cc
			case 0xb: {
				midiCc(msg);
				break;
			}
			// note off
			case 0x8: {
				midiNoteRelease(msg);
				break;
			}
			// note on
			case 0x9: {
				if (msg.getValue() > 0) {
					midiNotePress(msg);
				}
				else {
					// Many keyboards send a "note on" command with 0 velocity to mean "note release"
					midiNoteRelease(msg);
				}
				break;
			} 
			default: {
				break;
			}
		}
	}
	
	void midiCc(midi::Message msg) {
		uint8_t cc = msg.getNote();
		uint8_t value = msg.getValue();
		// Learn
		if (learningId != -1 && value > 0) {
			slot[learningId].cc = cc;
			slot[learningId].note = -1;
			if (mapCc[cc] != -1 && mapCc[cc] != learningId) 
				slot[mapCc[cc]].cc = -1;
			mapCc[cc] = learningId;
			learnedCc = true;
			commitLearn();
			updateMapLen();
			return;
		}
		// Send
		if (mapCc[cc] != -1) {
			processKey(mapCc[cc], value);
		}
	}

	void midiNotePress(midi::Message msg) {
		uint8_t note = msg.getNote();
		uint8_t vel = msg.getValue();
		// Learn
		if (learningId != -1 && vel > 0) {
			slot[learningId].cc = -1;
			slot[learningId].note = note;
			if (mapNote[note] != -1 && mapNote[note] != learningId) 
				slot[mapNote[note]].note = -1;
			mapNote[note] = learningId;
			learnedNote = true;
			commitLearn();
			updateMapLen();
			return;
		}
		// Send
		if (mapNote[note] != -1) {
			processKey(mapNote[note], vel);
		}
	}

	void midiNoteRelease(midi::Message msg) {
		uint8_t note = msg.getNote();
		if (mapNote[note] != -1) {
			processKey(mapNote[note], 0);
		}
	}

	void enableLearn(int id) {
		if (id == -1) {
			// Find next incomplete map
			while (++id < MAX_CHANNELS) {
				if (slot[id].cc < 0 && slot[id].note < 0 && slot[id].key < 0)
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
			learnedCc = false;
			learnedNote = false;
			learnedKey = false;
		}
		return;
	}

	void disableLearn() {
		learningId = -1;
	}

	void disableLearn(int id) {
		if (learningId == id) {
			learningId = -1;
		}
	}

	void commitLearn() {
		if (learningId == -1)
			return;
		if (!learnedCc && !learnedNote)
			return;
		if (!learnedKey && learningId >= 0)
			return;
		// Reset learned state
		learnedCc = false;
		learnedNote = false;
		learnedKey = false;
		learningId = -1;
	}

	void clearMap(int id, bool midiOnly = false) {
		learningId = -1;
		slot[id].cc = -1;
		slot[id].note = -1;
		if (!midiOnly) {
			slot[id].key = -1;
			slot[id].mods = 0;
		}
		updateMapLen();
	}

	void clearMaps() {
		learningId = -1;
		for (int id = 0; id < MAX_CHANNELS; id++) {
			slot[id].cc = -1;
			slot[id].note = -1;
			slot[id].key = -1;
			slot[id].mods = 0;
		}
		mapLen = 1;
	}

	void updateMapLen() {
		// Find last nonempty map
		int id;
		for (id = MAX_CHANNELS - 1; id >= 0; id--) {
			if (slot[id].cc >= 0 || slot[id].note >= 0 || slot[id].key >= 0)
				break;
		}
		mapLen = id + 1;
		// Add an empty "Mapping..." slot
		if (mapLen < MAX_CHANNELS) {
			mapLen++;
		}

		for (int i = 0; i < 128; i++) {
			mapCc[i] = -1;
			mapNote[i] = -1;
		}
		for (int i = 0; i < (int)slot.v.size(); i++) {
			if (slot.v[i].cc >= 0) {
				if (mapCc[slot.v[i].cc] != -1) slot[mapCc[slot.v[i].cc]].cc = -1;
				mapCc[slot.v[i].cc] = i < 3 ? (i - 4) : (i - 3);
			}
			if (slot.v[i].note >= 0) {
				if (mapNote[slot.v[i].note] != -1) slot[mapNote[slot.v[i].note]].note = -1;
				mapNote[slot.v[i].note] = i < 3 ? (i - 4) : (i - 3);
			}
		}
	}

	void learnKey(int key, int mods) {
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
					e.keyName = glfwGetKeyName(e.key, e.scancode);
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
		json_object_set_new(rootJ, "midiInput", midiInput.toJson());

		json_t* mapsJ = json_array();
		for (size_t i = 0; i < slot.v.size(); i++) {
			json_t* mapJ = json_object();
			json_object_set_new(mapJ, "key", json_integer(slot.v[i].key));
			json_object_set_new(mapJ, "mods", json_integer(slot.v[i].mods));
			json_object_set_new(mapJ, "cc", json_integer(slot.v[i].cc));
			json_object_set_new(mapJ, "note", json_integer(slot.v[i].note));
			json_object_set_new(mapJ, "moduleId", json_integer(slot.v[i].moduleId));
			json_array_append_new(mapsJ, mapJ);
		}
		json_object_set_new(rootJ, "maps", mapsJ);
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		panelTheme = json_integer_value(json_object_get(rootJ, "panelTheme"));
		json_t* midiInputJ = json_object_get(rootJ, "midiInput");
		midiInput.fromJson(midiInputJ);

		clearMaps();
		json_t* mapsJ = json_object_get(rootJ, "maps");
		json_t* mapJ;
		size_t i;
		json_array_foreach(mapsJ, i, mapJ) {
			slot.v[i].key = json_integer_value(json_object_get(mapJ, "key"));
			slot.v[i].mods = json_integer_value(json_object_get(mapJ, "mods"));
			slot.v[i].cc = json_integer_value(json_object_get(mapJ, "cc"));
			slot.v[i].note = json_integer_value(json_object_get(mapJ, "note"));
			json_t* moduleIdJ = json_object_get(mapJ, "moduleId");
			if (moduleIdJ) slot.v[i].moduleId = json_integer_value(moduleIdJ);
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
		if (module->slot[id].cc >= 0) {
			return string::f("cc%03d ", module->slot[id].cc);
		}
		else if (module->slot[id].note >= 0) {
			static const char* noteNames[] = {
				" C", "C#", " D", "D#", " E", " F", "F#", " G", "G#", " A", "A#", " B"
			};
			int oct = module->slot[id].note / 12 - 1;
			int semi = module->slot[id].note % 12;
			return string::f("  %s%d ", noteNames[semi], oct);
		}
		else if (module->slot[id].key >= 0 || id < -1) {
			return "..... ";
		}
		return "      ";
	}

	void step() override {
		if (!module) return;

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
				text = getSlotPrefix() + "mapping...";
			} else {
				text = getSlotPrefix() + "unmapped";
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
			ModuleWidget* mw = APP->scene->rack->getModule(module->slot[id].moduleId);
			std::string name = mw ? string::f("%s %s", mw->model->plugin->brand.c_str(), mw->module->model->name.c_str()) : "<ERROR>";
			menu->addChild(createMenuLabel(name));
			if (mw) menu->addChild(createMenuItem("Show", "", [=]() { Rack::ViewportCenter{mw}; }));
			menu->addChild(createMenuItem("Clear", "", [=]() { module->slot[id].moduleId = -1; }));
		}
		menu->addChild(createMenuItem("Learn", "", [=]() {
			module->moduleSelectProcessor.setOwner(APP->scene->rack->getModule(module->id));
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
			for (int id = 0; id < MAX_CHANNELS; id++) {
				choices[id]->visible = (id < mapLen);
				separators[id]->visible = (id < mapLen);
			}
		}
		LedDisplay::step();
	}

	void setModule(MODULE* module) {
		this->module = module;

		scroll = new ScrollWidget;
		scroll->box.size.x = box.size.x;
		scroll->box.size.y = box.size.y - scroll->box.pos.y;
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

		MidiWidget<>* midiInputWidget = createWidget<MidiWidget<>>(Vec(10.0f, 36.4f));
		midiInputWidget->box.size = Vec(130.0f, 67.0f);
		midiInputWidget->setMidiPort(module ? &module->midiInput : NULL);
		addChild(midiInputWidget);

		MidiKeyModDisplay<>* modWidget = createWidget<MidiKeyModDisplay<>>(Vec(10.0f, 107.4f));
		modWidget->box.size = Vec(130.0f, 67.0f);
		modWidget->setModule(module);
		addChild(modWidget);

		MidiKeyDisplay<16>* mapWidget = createWidget<MidiKeyDisplay<16>>(Vec(10.0f, 178.5f));
		mapWidget->box.size = Vec(130.0f, 164.7f);
		mapWidget->setModule(module);
		addChild(mapWidget);
	}

	void step() override {
		while (module && module->keyEventQueue.size() > 0) {
			std::tuple<event::HoverKey, int64_t> t = module->keyEventQueue.shift();
			event::HoverKey e = std::get<0>(t);
			int64_t moduleId = std::get<1>(t);
			
			if (moduleId != -1) {
				ModuleWidget* mw = APP->scene->rack->getModule(moduleId);
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