#include "plugin.hpp"
#include "components/MidiWidget.hpp"
#include "ui/keyboard.hpp"

namespace StoermelderPackOne {
namespace MidiKey {

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
		int cc = -1;
		/** [Stored to Json] */
		int note = -1;
		bool active = false;
	};

	struct SlotVector {
		std::vector<SlotData> vector{MAX_CHANNELS + 3};
		SlotData& operator[] (int index) {
			if (index < 0) { return vector[index + 4]; }
			return vector[index + 3];
		}
	};

	/** [Stored to Json] */
	SlotVector slot;

	bool activeCtrl = false;
	bool activeAlt = false;
	bool activeShift = false;
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

	dsp::RingBuffer<event::HoverKey, 8> keyEventQueue;

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
		for (int i = 0; i < MAX_CHANNELS; i++) {
			slot[i].cc = -1;
			slot[i].note = -1;
			slot[i].key = -1;
		}
		for (int i = 0; i < 128; i++) {
			mapCc[i] = -1;
			mapNote[i] = -1;
		}
		midiInput.reset();
	}

	void process(const ProcessArgs &args) override {
		midi::Message msg;
		while (midiInput.shift(&msg)) {
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
			learnedCc = true;
			commitLearn();
			updateMapLen();
			return;
		}

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
			learnedNote = true;
			commitLearn();
			updateMapLen();
			return;
		}

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

	void processKey(int id, uint8_t value) {
		switch (id) {
			case -4: activeCtrl = value > 0; return;
			case -3: activeAlt = value > 0; return;
			case -2: activeShift = value > 0; return;
			default: {
				if (value > 0 && slot[id].active) return;
				if (value == 0 && !slot[id].active) return;

				event::HoverKey e;
				e.key = slot[id].key;
				if (activeCtrl) e.mods = e.mods | RACK_MOD_CTRL;
				if (activeAlt) e.mods = e.mods | GLFW_MOD_ALT;
				if (activeShift) e.mods = e.mods | GLFW_MOD_SHIFT;
				e.action = value > 0 ? GLFW_PRESS : GLFW_RELEASE;
				keyEventQueue.push(e);
				slot[id].active = value > 0;
				return;
			}
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
		if (!midiOnly) slot[id].key = -1;
		updateMapLen();
	}

	void clearMaps() {
		learningId = -1;
		for (int id = 0; id < MAX_CHANNELS; id++) {
			slot[id].cc = -1;
			slot[id].note = -1;
			slot[id].key = -1;
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
		for (int i = 0; i < (int)slot.vector.size(); i++) {
			if (slot.vector[i].cc >= 0) mapCc[slot.vector[i].cc] = i < 3 ? (i - 4) : (i - 3);
			if (slot.vector[i].note >= 0) mapNote[slot.vector[i].note] = i < 3 ? (i - 4) : (i - 3);
		}
	}

	void learnKey(int key) {
		slot[learningId].key = key;
		learnedKey = true;
		commitLearn();
		updateMapLen();
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));
		json_object_set_new(rootJ, "midiInput", midiInput.toJson());

		json_t* mapsJ = json_array();
		for (size_t i = 0; i < slot.vector.size(); i++) {
			json_t* mapJ = json_object();
			json_object_set_new(mapJ, "key", json_integer(slot.vector[i].key));
			json_object_set_new(mapJ, "cc", json_integer(slot.vector[i].cc));
			json_object_set_new(mapJ, "note", json_integer(slot.vector[i].note));
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
			slot.vector[i].key = json_integer_value(json_object_get(mapJ, "key"));
			slot.vector[i].cc = json_integer_value(json_object_get(mapJ, "cc"));
			slot.vector[i].note = json_integer_value(json_object_get(mapJ, "note"));
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
		return "";
	}

	void step() override {
		if (!module) return;

		// Set bgColor and selected state
		if (module->learningId == id) {
			bgColor = color;
			bgColor.a = 0.15;
			if (APP->event->getSelectedWidget() != this)
				APP->event->setSelected(this);
		} 
		else {
			bgColor = nvgRGBA(0, 0, 0, 0);
			if (APP->event->getSelectedWidget() == this)
				APP->event->setSelected(NULL);
		}

		// Set text
		if ((module->slot[id].key >= 0 && module->learningId != id) || id < -1) {
			std::string prefix = getSlotPrefix();
			std::string label = "";
			switch (id) {
				case -4: label = RACK_MOD_CTRL_NAME; break;
				case -3: label = RACK_MOD_ALT_NAME; break;
				case -2: label = RACK_MOD_SHIFT_NAME; break;
				default: label = keyName(module->slot[id].key); break;
			}
			text = prefix + label;
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
		struct UnmapItem : MenuItem {
			MODULE* module;
			int id;
			void onAction(const event::Action& e) override {
				module->clearMap(id);
			}
		};

		struct UnmapMidiItem : MenuItem {
			MODULE* module;
			int id;
			void onAction(const event::Action& e) override {
				module->clearMap(id, true);
			}
		}; // struct UnmapMidiItem

		ui::Menu* menu = createMenu();
		menu->addChild(construct<UnmapItem>(&MenuItem::text, "Unmap", &UnmapItem::module, module, &UnmapItem::id, id));
		menu->addChild(construct<UnmapMidiItem>(&MenuItem::text, "Clear MIDI assignment", &UnmapMidiItem::module, module, &UnmapMidiItem::id, id));
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
			auto e = module->keyEventQueue.shift();
			APP->event->handleKey(APP->window->mousePos, e.key, 0, e.action, e.mods);
		}
		ThemedModuleWidget<MidiKeyModule<>>::step();
	}

	void onHoverKey(const event::HoverKey& e) override {
		if (module && module->learningId >= 0 && e.action == GLFW_PRESS) {
			int e_key = keyFix(e.key);
			std::string kn = keyName(e_key);
			if (!kn.empty()) {
				module->learnKey(e_key);
				e.consume(this);
			}
		}
		Widget::onHoverKey(e);
	}
};

} // namespace MidiKey
} // namespace StoermelderPackOne

Model* modelMidiKey = createModel<StoermelderPackOne::MidiKey::MidiKeyModule<>, StoermelderPackOne::MidiKey::MidiKeyWidget>("MidiKey");