#include "plugin.hpp"
#include "MidiCat.h"

namespace StoermelderPackOne {
namespace MidiCat {

struct MidiCatExModule : Module {
	enum ParamIds {
		PARAM_APPLY,
		NUM_PARAMS
	};
	enum InputIds {
		NUM_INPUTS
	};
	enum OutputIds {
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	/** [Stored to JSON] */
	int panelTheme = 0;
	/** [Stored to JSON] */
	std::map<std::pair<std::string, std::string>, MidimapModule*> midiMap;

	dsp::ClockDivider processDivider;

	MidiCatExpanderMessage* msg;
	bool connected;

	MidiCatExModule() {
		panelTheme = pluginSettings.panelThemeDefault;
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		processDivider.setDivision(24000);
		onReset();
	}

	void onReset() override {
		Module::onReset();
		resetMap();
	}

	void resetMap() {
		for (auto it : midiMap) {
			delete it.second;
		}
		midiMap.clear();
	}

	void process(const ProcessArgs& args) override {
		if (!processDivider.process()) return;
		connected = false;
		msg = NULL;

		Module* m = leftExpander.module;
		if (!m) return;
		if (!(m->model->plugin->slug == "Stoermelder-P1" && m->model->slug == "MidiCat")) return;
		if (!m->rightExpander.consumerMessage) return;
		msg = reinterpret_cast<MidiCatExpanderMessage*>(m->rightExpander.consumerMessage);

		connected = true;
	}

	void saveMidimap(int moduleId) {
		MidimapModule* m = new MidimapModule;
		Module* module;
		for (size_t i = 0; i < MAX_CHANNELS; i++) {
			if (msg->paramHandles[i].moduleId != moduleId) continue;
			module = msg->paramHandles[i].module;

			MidimapParam* p = new MidimapParam;
			p->paramId = msg->paramHandles[i].paramId;
			p->cc = msg->ccs[i];
			p->ccMode = msg->ccsMode[i];
			p->note = msg->notes[i];
			p->noteMode = msg->notesMode[i];
			p->label = msg->textLabel[i];
			m->paramMap.push_back(p);
		}
		m->pluginName = module->model->plugin->name;
		m->moduleName = module->model->name;

		auto p = std::pair<std::string, std::string>(module->model->plugin->slug, module->model->slug);
		auto it = midiMap.find(p);
		if (it != midiMap.end()) {
			delete it->second;
		}

		midiMap[p] = m;
	}

	void deleteMidimap(std::string pluginSlug, std::string moduleSlug) {
		auto p = std::pair<std::string, std::string>(pluginSlug, moduleSlug);
		auto it = midiMap.find(p);
		delete it->second;
		midiMap.erase(p);
	}

	void applyMidimap(Module* m) {
		if (!m) return;

		auto p = std::pair<std::string, std::string>(m->model->plugin->slug, m->model->slug);
		auto it = midiMap.find(p);
		if (it == midiMap.end()) return;
		MidimapModule* map = it->second;

		Module* exp = leftExpander.module;
		if (!exp) return;
		if (!(exp->model->plugin->slug == "Stoermelder-P1" && exp->model->slug == "MidiCat")) return;
		MidiCatProcessor* cat = dynamic_cast<MidiCatProcessor*>(exp);
		cat->moduleLearn(m->id, map->paramMap);
	}

	json_t* dataToJson() override {
		json_t *rootJ = json_object();
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));

		json_t* midiMapJ = json_array();
		for (auto it : midiMap) {
			json_t* midiMapJJ = json_object();
			json_object_set_new(midiMapJJ, "pluginSlug", json_string(it.first.first.c_str()));
			json_object_set_new(midiMapJJ, "moduleSlug", json_string(it.first.second.c_str()));

			auto a = it.second;
			json_object_set_new(midiMapJJ, "pluginName", json_string(a->pluginName.c_str()));
			json_object_set_new(midiMapJJ, "moduleName", json_string(a->moduleName.c_str()));
			json_t* paramMapJ = json_array();
			for (auto p : a->paramMap) {
				json_t* paramMapJJ = json_object();
				json_object_set_new(paramMapJJ, "paramId", json_integer(p->paramId));
				json_object_set_new(paramMapJJ, "cc", json_integer(p->cc));
				json_object_set_new(paramMapJJ, "ccMode", json_integer(p->ccMode));
				json_object_set_new(paramMapJJ, "note", json_integer(p->note));
				json_object_set_new(paramMapJJ, "noteMode", json_integer(p->noteMode));
				json_object_set_new(paramMapJJ, "label", json_string(p->label.c_str()));
				json_array_append_new(paramMapJ, paramMapJJ);
			}
			json_object_set_new(midiMapJJ, "paramMap", paramMapJ);

			json_array_append_new(midiMapJ, midiMapJJ);
		}
		json_object_set_new(rootJ, "midiMap", midiMapJ);

		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		panelTheme = json_integer_value(json_object_get(rootJ, "panelTheme"));

		resetMap();
		json_t* midiMapJ = json_object_get(rootJ, "midiMap");
		size_t i;
		json_t* midiMapJJ;
		json_array_foreach(midiMapJ, i, midiMapJJ) {
			std::string pluginSlug = json_string_value(json_object_get(midiMapJJ, "pluginSlug"));
			std::string moduleSlug = json_string_value(json_object_get(midiMapJJ, "moduleSlug"));

			MidimapModule* a = new MidimapModule;
			a->pluginName = json_string_value(json_object_get(midiMapJJ, "pluginName"));
			a->moduleName = json_string_value(json_object_get(midiMapJJ, "moduleName"));
			json_t* paramMapJ = json_object_get(midiMapJJ, "paramMap");
			size_t j;
			json_t* paramMapJJ;
			json_array_foreach(paramMapJ, j, paramMapJJ) {
				MidimapParam* p = new MidimapParam;
				p->paramId = json_integer_value(json_object_get(paramMapJJ, "paramId"));
				p->cc = json_integer_value(json_object_get(paramMapJJ, "cc"));
				p->ccMode = (CCMODE)json_integer_value(json_object_get(paramMapJJ, "ccMode"));
				p->note = json_integer_value(json_object_get(paramMapJJ, "note"));
				p->noteMode = (NOTEMODE)json_integer_value(json_object_get(paramMapJJ, "noteMode"));
				p->label = json_string_value(json_object_get(paramMapJJ, "label"));
				a->paramMap.push_back(p);
			}
			midiMap[std::pair<std::string, std::string>(pluginSlug, moduleSlug)] = a;
		}
	}
};


struct ApplyButton : TL1105 {
	ModuleWidget* mw;
	MidiCatExModule* module;
	bool learn = false;

	void onButton(const event::Button& e) override {
		if (!module) return;
		if (e.action == GLFW_PRESS) {
			learn = true;
			glfwSetCursor(APP->window->win, glfwCreateStandardCursor(GLFW_CROSSHAIR_CURSOR));
			APP->event->setSelected(this);
			e.consume(this);
		}
	}

	void onDeselect(const event::Deselect& e) override {
		if (!module) return;
		if (!learn) return;
		learn = false;
		glfwSetCursor(APP->window->win, NULL);

		// Learn module
		Widget* w = APP->event->getDraggedWidget();
		if (!w) return;
		ModuleWidget* mw = dynamic_cast<ModuleWidget*>(w);
		if (!mw) mw = w->getAncestorOfType<ModuleWidget>();
		if (!mw || mw == this->mw) return;
		Module* m = mw->module;
		if (!m) return;

		module->applyMidimap(m);
	}
};

struct MidiCatExWidget : ThemedModuleWidget<MidiCatExModule> {
	MidiCatExWidget(MidiCatExModule* module)
		: ThemedModuleWidget<MidiCatExModule>(module, "MidiCatEx") {
		setModule(module);

		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		ApplyButton* button = createParamCentered<ApplyButton>(Vec(15.0f, 306.7f), module, MidiCatExModule::PARAM_APPLY);
		button->mw = this;
		button->module = module;
		addParam(button);
	}

	void appendContextMenu(Menu* menu) override {
		ThemedModuleWidget<MidiCatExModule>::appendContextMenu(menu);
		MidiCatExModule* module = dynamic_cast<MidiCatExModule*>(this->module);
		assert(module);

		struct MapMenuItem : MenuItem {
			MidiCatExModule* module;
			MapMenuItem() {
				rightText = RIGHT_ARROW;
			}
			Menu* createChildMenu() override {
				struct MidimapModuleItem : MenuItem {
					MidiCatExModule* module;
					std::string pluginSlug;
					std::string moduleSlug;
					MidimapModule* midimapModule;
					MidimapModuleItem() {
						rightText = RIGHT_ARROW;
					}
					Menu* createChildMenu() override {
						struct DeleteItem : MenuItem {
							MidiCatExModule* module;
							std::string pluginSlug;
							std::string moduleSlug;
							void onAction(const event::Action& e) override {
								module->deleteMidimap(pluginSlug, moduleSlug);
							}
						}; // DeleteItem

						Menu* menu = new Menu;
						menu->addChild(construct<DeleteItem>(&MenuItem::text, "Delete", &DeleteItem::module, module, &DeleteItem::pluginSlug, pluginSlug, &DeleteItem::moduleSlug, moduleSlug));
						return menu;
					}
				}; // MidimapModuleItem

				std::list<std::pair<std::string, MidimapModuleItem*>> l; 
				for (auto it : module->midiMap) {
					MidimapModule* a = it.second;
					MidimapModuleItem* midimapModuleItem = new MidimapModuleItem;
					midimapModuleItem->text = string::f("%s - %s", a->pluginName.c_str(), a->moduleName.c_str());
					midimapModuleItem->module = module;
					midimapModuleItem->midimapModule = a;
					midimapModuleItem->pluginSlug = it.first.first;
					midimapModuleItem->moduleSlug = it.first.second;
					l.push_back(std::pair<std::string, MidimapModuleItem*>(midimapModuleItem->text, midimapModuleItem));
				}

				l.sort();
				Menu* menu = new Menu;
				for (auto it : l) {
					menu->addChild(it.second);
				}
				return menu;
			}
		}; // MapMenuItem

		struct SaveMenuItem : MenuItem {
			MidiCatExModule* module;
			std::list<std::pair<std::string, int>> list;

			SaveMenuItem(MidiCatExModule* module) {
				this->module = module;
				text = "Save mapping";
				rightText = RIGHT_ARROW;

				MidiCatExpanderMessage* msg = module->msg;
				std::set<int> s;
				for (size_t i = 0; i < MAX_CHANNELS; i++) {
					int moduleId = msg->paramHandles[i].moduleId;
					if (moduleId < 0) continue;
					if (s.find(moduleId) != s.end()) continue;
					s.insert(moduleId);
					Module* m = msg->paramHandles[i].module;
					if (!m) continue;
					std::string l = string::f("%s - %s", m->model->plugin->name.c_str(), m->model->name.c_str());
					auto p = std::pair<std::string, int>(l, moduleId);
					list.push_back(p);
				}
				list.sort();
			}

			Menu* createChildMenu() override {
				struct SaveItem : MenuItem {
					MidiCatExModule* module;
					int moduleId;
					void onAction(const event::Action& e) override {
						module->saveMidimap(moduleId);
					}
				}; // SaveItem

				Menu* menu = new Menu;
				for (auto it : list) {
					menu->addChild(construct<SaveItem>(&MenuItem::text, it.first, &SaveItem::module, module, &SaveItem::moduleId, it.second));
				}
				return menu;
			}
		}; // SaveMenuItem

		if (!module->connected) return;
		menu->addChild(new MenuSeparator());
		menu->addChild(construct<MapMenuItem>(&MenuItem::text, "Available mappings", &MapMenuItem::module, module));
		menu->addChild(new SaveMenuItem(module));
	}
};

} // namespace MidiCat
} // namespace StoermelderPackOne

Model* modelMidiCatEx = createModel<StoermelderPackOne::MidiCat::MidiCatExModule, StoermelderPackOne::MidiCat::MidiCatExWidget>("MidiCatEx");