#include "../../plugin.hpp"
#include "../../utils/StripIdFixModule.hpp"
#include "../../ui/ModuleSelectProcessor.hpp"
#include "../../ui/ViewportHelper.hpp"
#include "ReelPlacer.hpp"
#include <osdialog.h>
#include <chrono>

namespace StoermelderPackOne {
namespace Reel {

static const char SELECTION_FILTERS[] = "VCV Rack module selection (.vcvs):vcvs";


struct ReelModule : Module, StripIdFixModule {
	struct BoundModule {
		int64_t moduleId;
		std::string pluginSlug;
		std::string modelSlug;
		std::string moduleName;
		/** [Stored to JSON] Last known rack position, used for placement preview when the module is not in the rack */
		Vec pos = Vec(0.f, 0.f);
		ModuleWidget* getModuleWidget() { return APP->scene->rack->getModule(moduleId); }
	};

	struct ReelSlot {
		bool used = false;
		std::string label;
		std::vector<json_t*> moduleStates;
		json_t* cablesJ = nullptr;

		~ReelSlot() { clear(); }

		void clear() {
			for (json_t* vJ : moduleStates) {
				if (vJ) json_decref(vJ);
			}
			moduleStates.clear();
			if (cablesJ) {
				json_decref(cablesJ);
				cablesJ = nullptr;
			}
			used = false;
			label = "";
		}

		// Non-copyable — json_t* ownership is manual.
		// Custom move ops are required: the defaulted move would copy cablesJ as a
		// raw pointer without nulling the source, so the source's destructor would
		// json_decref the shared pointer and free it from under us. This caused
		// slot.cablesJ to dangle after std::vector reallocation on emplace_back.
		ReelSlot() = default;
		ReelSlot(const ReelSlot&) = delete;
		ReelSlot& operator=(const ReelSlot&) = delete;

		ReelSlot(ReelSlot&& other) noexcept
			: used(other.used)
			, label(std::move(other.label))
			, moduleStates(std::move(other.moduleStates))
			, cablesJ(other.cablesJ) {
			other.cablesJ = nullptr;
			other.used = false;
		}

		ReelSlot& operator=(ReelSlot&& other) noexcept {
			if (this != &other) {
				clear();
				used = other.used;
				label = std::move(other.label);
				moduleStates = std::move(other.moduleStates);
				cablesJ = other.cablesJ;
				other.cablesJ = nullptr;
				other.used = false;
			}
			return *this;
		}
	};

	/** [Stored to JSON] */
	int panelTheme = 0;
	/** [Stored to JSON] */
	std::vector<BoundModule*> boundModules;
	/** [Stored to JSON] Grows dynamically; never shrinks (cleared slots stay as empty entries) */
	std::vector<ReelSlot> slots;
	/** [Stored to JSON] Index of the last loaded slot, or -1 */
	int currentSlot = -1;
	/** [Stored to JSON] */
	bool boxDraw = false;
	/** [Stored to JSON] */
	NVGcolor boxColor;

	ReelModule() {
		panelTheme = pluginSettings.panelThemeDefault;
		boxColor = color::BLUE;
	}

	~ReelModule() {
		clearBoundModules();
	}

	void onReset(const Module::ResetEvent& e) override {
		slots.clear();
		clearBoundModules();
		currentSlot = -1;
		boxDraw = true;
		boxColor = color::BLUE;
		Module::onReset(e);
	}

	std::string bindModule(Module* m) {
		if (!m) return "";
		for (BoundModule* b : boundModules) {
			if (b->moduleId == m->id) return "";
		}
		BoundModule* b = new BoundModule;
		b->moduleId = m->id;
		b->moduleName = m->model->plugin->brand + " " + m->model->name;
		b->modelSlug = m->model->slug;
		b->pluginSlug = m->model->plugin->slug;
		boundModules.push_back(b);

		ModuleWidget* mw = b->getModuleWidget();
		if (mw) {
			b->pos = mw->box.pos;
		}
		return "";
	}

	void unbindModule(BoundModule* b) {
		for (auto it = boundModules.begin(); it != boundModules.end(); ++it) {
			if (*it == b) {
				boundModules.erase(it);
				break;
			}
		}
		delete b;
	}

	void clearBoundModules() {
		for (BoundModule* b : boundModules) delete b;
		boundModules.clear();
	}

	void remapSlotData(const std::map<int64_t, int64_t>& oldToNewIds) {
		for (ReelSlot& slot : slots) {
			if (!slot.used) continue;

			for (json_t* vJ : slot.moduleStates) {
				if (!vJ) continue;
				json_t* idJ = json_object_get(vJ, "id");
				if (!idJ) continue;
				int64_t oldId = json_integer_value(idJ);
				auto it = oldToNewIds.find(oldId);
				if (it != oldToNewIds.end()) {
					json_object_set_new(vJ, "id", json_integer(it->second));
				}
			}

			if (slot.cablesJ) {
				json_t* cableJ;
				size_t ci;
				json_array_foreach(slot.cablesJ, ci, cableJ) {
					json_t* outJ = json_object_get(cableJ, "outputModuleId");
					if (outJ) {
						int64_t oldId = json_integer_value(outJ);
						auto it = oldToNewIds.find(oldId);
						if (it != oldToNewIds.end())
							json_object_set_new(cableJ, "outputModuleId", json_integer(it->second));
					}
					json_t* inJ = json_object_get(cableJ, "inputModuleId");
					if (inJ) {
						int64_t oldId = json_integer_value(inJ);
						auto it = oldToNewIds.find(oldId);
						if (it != oldToNewIds.end())
							json_object_set_new(cableJ, "inputModuleId", json_integer(it->second));
					}
				}
			}
		}
	}

	/** Capture current state of all bound modules and their internal cables into slot i.
	 *  Grows the slots vector if needed. Must be called from the GUI thread. */
	void slotSave(int i) {
		if (i < 0) return;

		// Grow the slots vector as needed
		while ((int)slots.size() <= i) slots.emplace_back();

		ReelSlot& slot = slots[i];
		slot.clear();

		std::set<int64_t> moduleIds;
		for (BoundModule* b : boundModules) {
			moduleIds.insert(b->moduleId);
			// Update stored position from live widget
			ModuleWidget* mw = b->getModuleWidget();
			if (mw) b->pos = (mw->box.pos - RACK_OFFSET) / RACK_GRID_SIZE;
		}

		// Capture full module JSON for each bound module
		for (BoundModule* b : boundModules) {
			ModuleWidget* mw = b->getModuleWidget();
			if (!mw) {
				slot.moduleStates.push_back(nullptr);
				continue;
			}
			slot.moduleStates.push_back(mw->toJson());
		}

		// Capture cables where both endpoints are bound modules
		slot.cablesJ = json_array();
		for (BoundModule* b : boundModules) {
			ModuleWidget* mw = b->getModuleWidget();
			if (!mw) continue;
			for (PortWidget* output : mw->getOutputs()) {
				for (CableWidget* cw : APP->scene->rack->getCablesOnPort(output)) {
					if (!cw->isComplete()) continue;
					PortWidget* input = cw->inputPort;
					if (moduleIds.find(input->module->id) == moduleIds.end()) continue;

					json_t* cableJ = json_object();
					json_object_set_new(cableJ, "outputModuleId", json_integer(output->module->id));
					json_object_set_new(cableJ, "outputId", json_integer(output->portId));
					json_object_set_new(cableJ, "inputModuleId", json_integer(input->module->id));
					json_object_set_new(cableJ, "inputId", json_integer(input->portId));
					json_object_set_new(cableJ, "color", json_string(color::toHexString(cw->color).c_str()));
					json_array_append_new(slot.cablesJ, cableJ);
				}
			}
		}

		slot.used = true;
		currentSlot = i;
	}

	/** Restore state from slot i. Must be called from the GUI thread. */
	void slotLoad(int i) {
		if (i < 0 || i >= (int)slots.size() || !slots[i].used) return;
		ReelSlot& slot = slots[i];

		std::set<int64_t> moduleIds;
		for (BoundModule* b : boundModules) moduleIds.insert(b->moduleId);

		// Restore module states — match by stored module ID and plugin/model slug
		for (json_t* vJ : slot.moduleStates) {
			if (!vJ) continue;
			json_t* idJ = json_object_get(vJ, "id");
			json_t* pluginJ = json_object_get(vJ, "plugin");
			json_t* modelJ = json_object_get(vJ, "model");
			if (!idJ || !pluginJ || !modelJ) continue;
			int64_t moduleId = json_integer_value(idJ);

			for (BoundModule* b : boundModules) {
				if (b->moduleId != moduleId) continue;
				if (b->pluginSlug != json_string_value(pluginJ) || b->modelSlug != json_string_value(modelJ)) break;
				ModuleWidget* mw = b->getModuleWidget();
				if (mw) mw->fromJson(vJ);
				break;
			}
		}

		// Remove all cables between bound modules — iterate cableContainer directly
		std::vector<CableWidget*> toRemove;
		for (CableWidget* cw : APP->scene->rack->getCompleteCables()) {
			if (moduleIds.count(cw->cable->outputModule->id) &&
			    moduleIds.count(cw->cable->inputModule->id)) {
				toRemove.push_back(cw);
			}
		}
		for (CableWidget* cw : toRemove) {
			APP->scene->rack->removeCable(cw);
			delete cw; // destructor calls engine->removeCable
		}

		// Recreate cables from the saved slot
		if (slot.cablesJ) {
			json_t* cableJ;
			size_t cableIndex;
			json_array_foreach(slot.cablesJ, cableIndex, cableJ) {
				int64_t outputModuleId = json_integer_value(json_object_get(cableJ, "outputModuleId"));
				int outputId = json_integer_value(json_object_get(cableJ, "outputId"));
				int64_t inputModuleId = json_integer_value(json_object_get(cableJ, "inputModuleId"));
				int inputId = json_integer_value(json_object_get(cableJ, "inputId"));
				const char* colorStr = json_string_value(json_object_get(cableJ, "color"));

				ModuleWidget* outputMw = APP->scene->rack->getModule(outputModuleId);
				ModuleWidget* inputMw = APP->scene->rack->getModule(inputModuleId);
				if (!outputMw || !inputMw) continue;

				engine::Cable* c = new engine::Cable;
				c->outputModule = outputMw->module;
				c->outputId = outputId;
				c->inputModule = inputMw->module;
				c->inputId = inputId;
				APP->engine->addCable(c);

				CableWidget* cw = new CableWidget;
				try {
					cw->setCable(c);
				} catch (Exception& e) {
					delete cw;
					APP->engine->removeCable(c);
					delete c;
					continue;
				}
				if (colorStr) cw->color = color::fromHexString(colorStr);
				APP->scene->rack->addCable(cw);
			}
		}

		currentSlot = i;
	}

	/** Randomize module states and/or cables between bound modules. GUI thread only. */
	void slotRandomize(bool randomizeState, bool randomizeCables) {
		if (boundModules.empty()) return;

		std::set<int64_t> moduleIds;
		for (BoundModule* b : boundModules) moduleIds.insert(b->moduleId);

		if (randomizeState) {
			for (BoundModule* b : boundModules) {
				ModuleWidget* mw = b->getModuleWidget();
				if (mw && mw->module) {
					Module::RandomizeEvent e;
					mw->module->onRandomize(e);
				}
			}
		}

		if (randomizeCables) {
			// Remove all cables between bound modules
			std::vector<CableWidget*> toRemove;
			for (CableWidget* cw : APP->scene->rack->getCompleteCables()) {
				if (moduleIds.count(cw->cable->outputModule->id) &&
				    moduleIds.count(cw->cable->inputModule->id)) {
					toRemove.push_back(cw);
				}
			}
			for (CableWidget* cw : toRemove) {
				APP->scene->rack->removeCable(cw);
				delete cw;
			}

			// Collect available output and input ports across all bound modules
			std::vector<PortWidget*> outputs;
			std::vector<PortWidget*> inputs;
			for (BoundModule* b : boundModules) {
				ModuleWidget* mw = b->getModuleWidget();
				if (!mw) continue;
				for (PortWidget* pw : mw->getOutputs()) outputs.push_back(pw);
				for (PortWidget* pw : mw->getInputs()) inputs.push_back(pw);
			}

			// Shuffle both lists for random pairings
			std::shuffle(outputs.begin(), outputs.end(), random::local());
			std::shuffle(inputs.begin(), inputs.end(), random::local());

			// For each output, randomly decide whether to connect it to an unoccupied input
			std::set<PortWidget*> usedInputs;
			for (PortWidget* output : outputs) {
				if (random::uniform() < 0.5f) continue;
				for (PortWidget* input : inputs) {
					if (usedInputs.count(input)) continue;
					engine::Cable* c = new engine::Cable;
					c->outputModule = output->module;
					c->outputId = output->portId;
					c->inputModule = input->module;
					c->inputId = input->portId;
					APP->engine->addCable(c);
					CableWidget* cw = new CableWidget;
					try {
						cw->setCable(c);
					} catch (Exception&) {
						delete cw;
						APP->engine->removeCable(c);
						delete c;
						continue;
					}
					if (!settings::cableColors.empty())
						cw->color = settings::cableColors[random::u32() % settings::cableColors.size()];
					APP->scene->rack->addCable(cw);
					usedInputs.insert(input);
					break;
				}
			}
		}
	}

	void slotDelete(int i) {
		if (i < 0 || i >= (int)slots.size()) return;
		slots.erase(slots.begin() + i);
		if (currentSlot == i) currentSlot = -1;
		else if (currentSlot > i) currentSlot--;
	}

	void slotCopyPaste(int src, int dst) {
		if (src < 0 || src >= (int)slots.size() || !slots[src].used) return;
		if (dst < 0) return;

		while ((int)slots.size() <= dst) slots.emplace_back();

		ReelSlot& srcSlot = slots[src];
		ReelSlot& dstSlot = slots[dst];
		dstSlot.clear();

		for (json_t* vJ : srcSlot.moduleStates) {
			dstSlot.moduleStates.push_back(vJ ? json_deep_copy(vJ) : nullptr);
		}
		if (srcSlot.cablesJ) dstSlot.cablesJ = json_deep_copy(srcSlot.cablesJ);
		dstSlot.label = srcSlot.label;
		dstSlot.used = true;
	}

	bool isBoxActive() {
		return boxDraw && !Module::isBypassed();
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));
		json_object_set_new(rootJ, "currentSlot", json_integer(currentSlot));
		json_object_set_new(rootJ, "boxDraw", json_boolean(boxDraw));
		json_object_set_new(rootJ, "boxColor", json_string(color::toHexString(boxColor).c_str()));

		json_t* boundModulesJ = json_array();
		for (BoundModule* b : boundModules) {
			json_t* bJ = json_object();
			json_object_set_new(bJ, "moduleId", json_integer(b->moduleId));
			json_object_set_new(bJ, "pluginSlug", json_string(b->pluginSlug.c_str()));
			json_object_set_new(bJ, "modelSlug", json_string(b->modelSlug.c_str()));
			json_object_set_new(bJ, "moduleName", json_string(b->moduleName.c_str()));
			json_object_set_new(bJ, "pos", json_pack("[f, f]", b->pos.x, b->pos.y));
			json_array_append_new(boundModulesJ, bJ);
		}
		json_object_set_new(rootJ, "boundModules", boundModulesJ);

		json_t* slotsJ = json_array();
		for (ReelSlot& slot : slots) {
			json_t* slotJ = json_object();
			json_object_set_new(slotJ, "used", json_boolean(slot.used));
			json_object_set_new(slotJ, "label", json_string(slot.label.c_str()));
			if (slot.used) {
				json_t* moduleStatesJ = json_array();
				for (json_t* vJ : slot.moduleStates) {
					json_array_append(moduleStatesJ, vJ ? vJ : json_null());
				}
				json_object_set_new(slotJ, "moduleStates", moduleStatesJ);
				if (slot.cablesJ) {
					json_object_set(slotJ, "cables", slot.cablesJ);
				}
			}
			json_array_append_new(slotsJ, slotJ);
		}
		json_object_set_new(rootJ, "slots", slotsJ);

		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		panelTheme = json_integer_value(json_object_get(rootJ, "panelTheme"));

		json_t* currentSlotJ = json_object_get(rootJ, "currentSlot");
		if (currentSlotJ) currentSlot = json_integer_value(currentSlotJ);

		json_t* boxDrawJ = json_object_get(rootJ, "boxDraw");
		if (boxDrawJ) boxDraw = json_boolean_value(boxDrawJ);
		json_t* boxColorJ = json_object_get(rootJ, "boxColor");
		if (boxColorJ) boxColor = color::fromHexString(json_string_value(boxColorJ));

		for (BoundModule* b : boundModules) delete b;
		boundModules.clear();

		json_t* boundModulesJ = json_object_get(rootJ, "boundModules");
		if (boundModulesJ) {
			json_t* bJ;
			size_t bIndex;
			json_array_foreach(boundModulesJ, bIndex, bJ) {
				BoundModule* b = new BoundModule;
				b->moduleId = idFix(json_integer_value(json_object_get(bJ, "moduleId")));
				json_t* pluginSlugJ = json_object_get(bJ, "pluginSlug");
				json_t* modelSlugJ  = json_object_get(bJ, "modelSlug");
				json_t* moduleNameJ = json_object_get(bJ, "moduleName");
				b->pluginSlug  = pluginSlugJ  ? json_string_value(pluginSlugJ)  : "";
				b->modelSlug   = modelSlugJ   ? json_string_value(modelSlugJ)   : "";
				b->moduleName  = moduleNameJ  ? json_string_value(moduleNameJ)  : "";
				json_t* posJ = json_object_get(bJ, "pos");
				double x = 0.0, y = 0.0;
				json_unpack(posJ, "[F, F]", &x, &y);
				b->pos = Vec(x, y);
				boundModules.push_back(b);
			}
		}

		slots.clear();
		json_t* slotsJ = json_object_get(rootJ, "slots");
		if (slotsJ) {
			json_t* slotJ;
			size_t slotIndex;
			json_array_foreach(slotsJ, slotIndex, slotJ) {
				slots.emplace_back();
				ReelSlot& slot = slots.back();
				slot.used = json_boolean_value(json_object_get(slotJ, "used"));
				json_t* labelJ = json_object_get(slotJ, "label");
				if (labelJ) slot.label = json_string_value(labelJ);

				if (slot.used) {
					json_t* moduleStatesJ = json_object_get(slotJ, "moduleStates");
					if (moduleStatesJ) {
						json_t* vJ;
						size_t vIndex;
						json_array_foreach(moduleStatesJ, vIndex, vJ) {
							if (json_is_null(vJ)) {
								slot.moduleStates.push_back(nullptr);
							} else {
								json_t* copy = json_deep_copy(vJ);
								json_t* idJ = json_object_get(copy, "id");
								if (idJ) {
									json_object_set_new(copy, "id", json_integer(idFix(json_integer_value(idJ))));
								}
								slot.moduleStates.push_back(copy);
							}
						}
					}

					json_t* cablesJ = json_object_get(slotJ, "cables");
					if (cablesJ) {
						slot.cablesJ = json_deep_copy(cablesJ);
						json_t* cableJ;
						size_t cableIndex;
						json_array_foreach(slot.cablesJ, cableIndex, cableJ) {
							json_t* outIdJ = json_object_get(cableJ, "outputModuleId");
							if (outIdJ) {
								json_object_set_new(cableJ, "outputModuleId",
									json_integer(idFix(json_integer_value(outIdJ))));
							}
							json_t* inIdJ = json_object_get(cableJ, "inputModuleId");
							if (inIdJ) {
								json_object_set_new(cableJ, "inputModuleId",
									json_integer(idFix(json_integer_value(inIdJ))));
							}
						}
					}
				}
			}
		}

		idFixClearMap();
	}
};


template <class MODULE>
struct ReelBoundsDrawer : Widget {
	MODULE* module = NULL;

	void draw(const DrawArgs& args) override {
		if (!module || !module->isBoxActive()) return;

		Rect viewPort = getViewport(box);
		for (typename MODULE::BoundModule* b : module->boundModules) {
			ModuleWidget* mw = APP->scene->rack->getModule(b->moduleId);
			if (!mw) continue;

			Vec p1 = mw->getRelativeOffset(Vec(), this);
			Vec p = getAbsoluteOffset(Vec()).neg().plus(p1).div(APP->scene->rackScroll->zoomWidget->zoom);

			if (viewPort.isIntersecting(Rect(p, mw->box.size))) {
				nvgSave(args.vg);
				nvgResetScissor(args.vg);
				nvgTranslate(args.vg, p.x, p.y);
				nvgBeginPath(args.vg);
				nvgRect(args.vg, 1.f, 1.f, mw->box.size.x - 2.f, mw->box.size.y - 2.f);
				nvgStrokeColor(args.vg, module->boxColor);
				nvgStrokeWidth(args.vg, 2.f);
				nvgStroke(args.vg);
				nvgRestore(args.vg);
			}
		}
		Widget::draw(args);
	}
};


struct ReelSlotEntry : LedDisplayChoice {
	static constexpr float LOAD_ZONE_W = 26.f;

	ReelModule* module = NULL;
	bool processEvents = true;
	int id = 0;

	// Display scrolling
	std::chrono::time_point<std::chrono::system_clock> hscrollUpdate;
	int hscrollCharOffset = 0;

	// Inline label editing state
	bool editMode = false;
	std::string editText;
	int editCursor = 0;
	bool editEscaped = false;
	std::chrono::time_point<std::chrono::system_clock> cursorBlinkTime;
	bool cursorVisible = true;

	// Copy/paste slot index shared between all instances
	static int copySlot;

	ReelSlotEntry() {
		box.size = mm2px(Vec(0, 8.0));
		textOffset = Vec(4.f, box.size.y * 0.5f + 3.f);
		hscrollUpdate = std::chrono::system_clock::now();
		cursorBlinkTime = std::chrono::system_clock::now();
	}

	void setModule(ReelModule* m) { module = m; }

	bool isTrailing() const { return !module || id >= (int)module->slots.size(); }
	bool isUsed() const { return !isTrailing() && module->slots[id].used; }
	bool isActive() const { return module && module->currentSlot == id; }

	void step() override {
		auto now = std::chrono::system_clock::now();

		// ---- Edit mode ----
		if (editMode) {
			if (now - cursorBlinkTime > std::chrono::milliseconds{530}) {
				cursorVisible = !cursorVisible;
				cursorBlinkTime = now;
			}
			textOffset.x = LOAD_ZONE_W + 4.f;
			bgColor = nvgRGBA(0x20, 0x3a, 0x60, 0x90);
			color = nvgRGB(0xcc, 0xdd, 0xff);
			std::string cur = cursorVisible ? "|" : " ";
			text = editText.substr(0, editCursor) + cur + editText.substr(editCursor);
			return;
		}

		textOffset.x = LOAD_ZONE_W + 4.f;

		// ---- Normal display ----
		if (!module) {
			text = "—";
			color = nvgRGBA(0xf0, 0xf0, 0xf0, 0x80);
			bgColor = nvgRGBA(0, 0, 0, 0);
			return;
		}

		bool trailing = isTrailing();
		bool used = isUsed();
		bool active = isActive();

		bgColor = active ? nvgRGBA(0xf0, 0xf0, 0xf0, 37) : nvgRGBA(0, 0, 0, 0);

		if (trailing || !used) {
			text = "—";
			color = nvgRGBA(0xf0, 0xf0, 0xf0, 0x28);
		} 
		else {
			const std::string& rawLabel = module->slots[id].label;
			std::string label = rawLabel.empty() ? "Snapshot" : rawLabel;

			// Scroll long labels horizontally
			size_t maxLen = (size_t)std::ceil((box.size.x - LOAD_ZONE_W - 4.f) / 6.5f);
			if (!rawLabel.empty() && rawLabel.length() > maxLen) {
				if (now - hscrollUpdate > std::chrono::milliseconds{100}) {
					hscrollCharOffset = (hscrollCharOffset + 1) % (int)(rawLabel.length() + maxLen);
					hscrollUpdate = now;
				}
				int offset = std::min(hscrollCharOffset, (int)rawLabel.length());
				text = rawLabel.substr(offset);
			} 
			else {
				hscrollCharOffset = 0;
				text = label;
			}

			color = active ? nvgRGB(0xf0, 0xf0, 0xf0) : nvgRGBA(0xf0, 0xf0, 0xf0, 0x95);
		}
	}

	void draw(const DrawArgs& args) override {
		LedDisplayChoice::draw(args);

		// Thin vertical divider between load zone and label zone
		nvgBeginPath(args.vg);
		nvgMoveTo(args.vg, LOAD_ZONE_W, 2.f);
		nvgLineTo(args.vg, LOAD_ZONE_W, box.size.y - 2.f);
		nvgStrokeColor(args.vg, editMode ? nvgRGBA(0x60, 0x90, 0xcc, 0x80) : nvgRGBA(0xff, 0xff, 0xff, 0x18));
		nvgStrokeWidth(args.vg, 0.7f);
		nvgStroke(args.vg);

		// Load-zone icon: filled triangle (▶) for used slots, hollow for unused
		if (module && !isTrailing() && isUsed()) {
			float cx = LOAD_ZONE_W / 2.f;
			float cy = box.size.y / 2.f;
			float h  = 5.f;
			float w  = h * 0.866f;
			nvgBeginPath(args.vg);
			nvgMoveTo(args.vg, cx - w / 2.f, cy - h / 2.f);
			nvgLineTo(args.vg, cx + w / 2.f, cy);
			nvgLineTo(args.vg, cx - w / 2.f, cy + h / 2.f);
			nvgClosePath(args.vg);
			nvgFillColor(args.vg, isActive()
				? nvgRGB(0xf0, 0xf0, 0xf0)
				: nvgRGBA(0xf0, 0xf0, 0xf0, 0xa5));
			nvgFill(args.vg);
		}
	}

	void onButton(const event::Button& e) override {
		if (!processEvents) return;
		e.stopPropagating();
		if (!module) return;

		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			e.consume(this);
			// Deselect any other widget currently in edit mode
			if (APP->event->getSelectedWidget() != this)
				APP->event->setSelectedWidget(nullptr);

			if (e.pos.x <= LOAD_ZONE_W) {
				// Load zone: load the slot
				if (!isTrailing() && isUsed()) module->slotLoad(id);
			} else {
				// Label zone: enter inline label edit
				APP->event->setSelectedWidget(this);
			}
		}

		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_RIGHT) {
			e.consume(this);
			openContextMenu();
		}
	}

	// ---- Inline edit event handlers ----

	void onSelect(const event::Select& e) override {
		if (!processEvents) return;
		editMode = true;
		editText = (!isTrailing() && id < (int)module->slots.size()) ? module->slots[id].label : "";
		editCursor = (int)editText.length();
		editEscaped = false;
		cursorVisible = true;
		cursorBlinkTime = std::chrono::system_clock::now();
	}

	void onDeselect(const event::Deselect& e) override {
		if (editMode) {
			if (!editEscaped && module && id < (int)module->slots.size()) {
				module->slots[id].label = editText;
			}
			editMode = false;
		}
	}

	void onSelectText(const event::SelectText& e) override {
		if (!editMode) return;
		// Insert printable ASCII/UTF-8 characters at cursor
		if (e.codepoint >= 32 && e.codepoint < 0x10000) {
			char buf[8] = {};
			if (e.codepoint < 0x80) {
				buf[0] = (char)e.codepoint;
			} else if (e.codepoint < 0x800) {
				buf[0] = 0xC0 | (char)(e.codepoint >> 6);
				buf[1] = 0x80 | (char)(e.codepoint & 0x3F);
			} else {
				buf[0] = 0xE0 | (char)(e.codepoint >> 12);
				buf[1] = 0x80 | (char)((e.codepoint >> 6) & 0x3F);
				buf[2] = 0x80 | (char)(e.codepoint & 0x3F);
			}
			std::string ch = buf;
			editText.insert(editCursor, ch);
			editCursor += (int)ch.length();
		}
		e.consume(this);
	}

	void onSelectKey(const event::SelectKey& e) override {
		if (!editMode) return;
		if (e.action != GLFW_PRESS && e.action != GLFW_REPEAT) return;

		switch (e.key) {
			case GLFW_KEY_ENTER:
			case GLFW_KEY_KP_ENTER:
				if (module && id < (int)module->slots.size())
					module->slots[id].label = editText;
				APP->event->setSelectedWidget(nullptr);
				break;
			case GLFW_KEY_ESCAPE:
				editEscaped = true;
				APP->event->setSelectedWidget(nullptr);
				break;
			case GLFW_KEY_BACKSPACE:
				if (editCursor > 0) { editText.erase(editCursor - 1, 1); editCursor--; }
				break;
			case GLFW_KEY_DELETE:
				if (editCursor < (int)editText.length()) editText.erase(editCursor, 1);
				break;
			case GLFW_KEY_LEFT:
				if (editCursor > 0) editCursor--;
				break;
			case GLFW_KEY_RIGHT:
				if (editCursor < (int)editText.length()) editCursor++;
				break;
			case GLFW_KEY_HOME:
				editCursor = 0;
				break;
			case GLFW_KEY_END:
				editCursor = (int)editText.length();
				break;
			default:
				return; // don't consume unknown keys
		}
		e.consume(this);
	}

	void openContextMenu() {
		bool trailing = isTrailing();
		bool used = isUsed();

		ui::Menu* menu = createMenu();
		std::string title = (!trailing && !module->slots[id].label.empty())
			? module->slots[id].label
			: string::f("Slot %d", id + 1);
		menu->addChild(createMenuLabel(title));

		menu->addChild(createMenuItem("Save current state", "", [=]() {
			if (!module || module->boundModules.empty()) {
				osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK,
					"No modules bound to REEL. Right-click the REEL module to bind modules first.");
				return;
			}
			module->slotSave(id);
		}));

		menu->addChild(createMenuItem("Load", "", [=]() {
			module->slotLoad(id);
		}, !used));
		menu->addChild(createMenuItem("Delete", "", [=]() {
			module->slotDelete(id);
		}, !used));

		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuItem("Copy", "", [=]() {
			copySlot = id;
		}, !used));
		menu->addChild(createMenuItem("Paste", "", [=]() {
			if (copySlot >= 0 && module) module->slotCopyPaste(copySlot, id);
		}, copySlot < 0));
	}
};

int ReelSlotEntry::copySlot = -1;


/** Scrollable list of ReelSlotEntry rows.
 *  Grows dynamically as module->slots expands. */
struct ReelDisplay : LedDisplay {
	ReelModule* module = NULL;
	ScrollWidget* scroll = NULL;
	std::vector<ReelSlotEntry*> entries;
	std::vector<LedDisplaySeparator*> separators;
	float nextEntryY = 0.f;

	~ReelDisplay() {
		for (ReelSlotEntry* e : entries) e->processEvents = false;
	}

	void setModule(ReelModule* m) {
		module = m;

		scroll = new ScrollWidget;
		scroll->box.pos.y = 2.f;
		scroll->box.size.x = box.size.x;
		scroll->box.size.y = box.size.y - 4.f;
		addChild(scroll);

		nextEntryY = 0.f;
	}

	/** Append one new entry to the scroll container. */
	void addEntry(int id) {
		// Separator line overlaid at top of every row except the first
		if (id > 0) {
			LedDisplaySeparator* sep = createWidget<LedDisplaySeparator>(Vec(0.f, nextEntryY));
			sep->box.size.x = box.size.x;
			scroll->container->addChild(sep);
			separators.push_back(sep);
		} else {
			separators.push_back(nullptr);
		}

		ReelSlotEntry* entry = createWidget<ReelSlotEntry>(Vec(0.f, nextEntryY));
		entry->box.size.x = box.size.x;
		entry->id = id;
		entry->setModule(module);
		scroll->container->addChild(entry);
		entries.push_back(entry);

		nextEntryY = entry->box.getBottomLeft().y;
	}

	void step() override {
		if (module) {
			int needed = (int)module->slots.size() + 1;
			while ((int)entries.size() < needed) addEntry((int)entries.size());
			while ((int)entries.size() > needed) {
				ReelSlotEntry* entry = entries.back();
				entry->processEvents = false;
				scroll->container->removeChild(entry);
				delete entry;
				entries.pop_back();

				Widget* sep = separators.back();
				if (sep) {
					scroll->container->removeChild(sep);
					delete sep;
				}
				separators.pop_back();

				nextEntryY = entries.empty() ? 0.f : entries.back()->box.getBottomLeft().y;
			}
		}
		LedDisplay::step();
	}
};


struct ReelWidget : ThemedModuleWidget<ReelModule> {
	typedef ThemedModuleWidget<ReelModule> BASE;
	ReelModule* module;

	ReelBoundsDrawer<ReelModule>* boxDrawer = NULL;
	ReelPlacerContainer* placerContainer = NULL;
	ModuleSelectProcessor moduleSelectProcessor;
	std::string moduleSelectProcessorStr;

	ReelWidget(ReelModule* module)
		: ThemedModuleWidget<ReelModule>(module, "Reel") {
		BASE::setModule(module);
		this->module = module;

		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		if (module) {
			boxDrawer = new ReelBoundsDrawer<ReelModule>;
			boxDrawer->module = module;
			APP->scene->rack->addChild(boxDrawer);

			placerContainer = new ReelPlacerContainer;
			APP->scene->rack->addChild(placerContainer);

			// Keep cable container on top so module outlines render below cables
			std::list<Widget*>::iterator it;
			for (it = APP->scene->rack->children.begin(); it != APP->scene->rack->children.end(); ++it) {
				if (*it == APP->scene->rack->getCableContainer()) break;
			}
			if (it != APP->scene->rack->children.end()) {
				APP->scene->rack->children.splice(APP->scene->rack->children.end(),
					APP->scene->rack->children, it);
			}
		}

		ReelDisplay* display = createWidget<ReelDisplay>(Vec(0.f, 36.4f));
		display->box.size = Vec(BASE::box.size.x, 307.0f);
		display->setModule(module);
		BASE::addChild(display);
	}

	~ReelWidget() {
		if (placerContainer) {
			APP->scene->rack->removeChild(placerContainer);
			delete placerContainer;
		}
		if (boxDrawer) {
			APP->scene->rack->removeChild(boxDrawer);
			delete boxDrawer;
		}
	}

	void onDeselect(const event::Deselect& e) override {
		BASE::onDeselect(e);
		moduleSelectProcessor.processDeselect();
	}

	/** Spawn new instances of every bound module using stored relative positions,
	 *  rebind, and remap all stored slot IDs to the new modules. */
	/** Unified module placement handler for both bound modules and selection imports.
	 *  moduleInfos: pairs of (model, relative position)
	 *  relativePositions: parallel array of relative positions for each module
	 *  bindAfterCreate: if true, bind created modules to this Reel
	 *  rootJ: optional JSON root for cable recreation (may be NULL)
	 *  idMap: optional map for remapping IDs (used for bound module recreation)
	 */
	void importModulesAt(Vec mousePos,
			const std::vector<std::pair<plugin::Model*, Vec>>& moduleInfos,
			const std::vector<Vec>& relativePositions,
			bool bindAfterCreate,
			json_t* rootJ,
			std::map<int64_t, ModuleWidget*>* idMap) {
		if (!module) return;

		std::map<int64_t, ModuleWidget*> modules;
		if (idMap) modules = *idMap;

		history::ComplexAction* complexAction = new history::ComplexAction;
		complexAction->name = bindAfterCreate ? "REEL import selection" : "REEL recreate bound modules";

		// Create modules
		for (size_t i = 0; i < moduleInfos.size(); i++) {
			plugin::Model* model = moduleInfos[i].first;
			Vec relPos = (i < relativePositions.size()) ? relativePositions[i] : Vec(0, 0);
			Vec pos = mousePos.plus(relPos);
			int64_t oldId = -1;

			// For bound modules, get oldId from BoundModule
			if (i < module->boundModules.size()) {
				oldId = module->boundModules[i]->moduleId;
			}

			engine::Module* addedModule = model->createModule();
			APP->engine->addModule(addedModule);

			ModuleWidget* mw = model->createModuleWidget(addedModule);
			APP->scene->rack->addModule(mw);
			APP->scene->rack->setModulePosForce(mw, pos);
			APP->scene->rack->select(mw);

			// Track widget - use oldId if available, otherwise use index
			if (oldId >= 0) {
				modules[oldId] = mw;
			} else {
				modules[addedModule->id] = mw;
			}

			history::ModuleAdd* h = new history::ModuleAdd;
			h->name = bindAfterCreate ? "create module" : "REEL add module";
			h->setModule(mw);
			complexAction->push(h);
		}

		APP->history->push(complexAction);

		// Remap slot data IDs if needed (for bound module recreation)
		std::map<int64_t, int64_t> oldToNewIds;
		for (auto& kv : modules) {
			if (kv.second) oldToNewIds[kv.first] = kv.second->module->id;
		}
		module->remapSlotData(oldToNewIds);
		module->currentSlot = -1;

		// Update the IDs of bound modules (for bound module recreation)
		for (ReelModule::BoundModule* b : module->boundModules) {
			auto it = modules.find(b->moduleId);
			if (it != modules.end() && it->second) {
				b->moduleId = it->second->module->id;
			}
		}

		// Load presets and bind for selection import
		if (bindAfterCreate && rootJ) {
			// Load presets from JSON - match modules by position
			json_t* modulesJ = json_object_get(rootJ, "modules");
			if (modulesJ) {
				size_t moduleIndex;
				json_t* moduleJ;
				int idx = 0;
				json_array_foreach(modulesJ, moduleIndex, moduleJ) {
					if (idx >= (int)moduleInfos.size()) break;

					// Find widget by position matching
					ModuleWidget* mw = NULL;
					Vec relPos = (idx < (int)relativePositions.size()) ? relativePositions[idx] : Vec(0.f, 0.f);
					Vec expectedPos = mousePos.plus(relPos);
					for (auto& kv : modules) {
						Vec diff = kv.second->box.pos.minus(expectedPos);
						if (std::abs(diff.x) < 1.f && std::abs(diff.y) < 1.f) {
							mw = kv.second;
							break;
						}
					}
					if (!mw) {
						idx++;
						continue;
					}

					StripIdFixModule* m = dynamic_cast<StripIdFixModule*>(mw->module);
					if (m) m->idFixDataFromJson(modules);

					mw->fromJson(moduleJ);
					idx++;
				}
			}

			// Load cables
			json_t* cablesJ = json_object_get(rootJ, "cables");
			if (cablesJ) {
				json_t* cableJ;
				size_t cableIndex;
				json_array_foreach(cablesJ, cableIndex, cableJ) {
					int64_t outputModuleId = json_integer_value(json_object_get(cableJ, "outputModuleId"));
					int outputId = json_integer_value(json_object_get(cableJ, "outputId"));
					int64_t inputModuleId = json_integer_value(json_object_get(cableJ, "inputModuleId"));
					int inputId = json_integer_value(json_object_get(cableJ, "inputId"));
					const char* colorStr = json_string_value(json_object_get(cableJ, "color"));

					ModuleWidget* outputModule = NULL;
					ModuleWidget* inputModule = NULL;
					for (auto& kv : modules) {
						if (kv.first == outputModuleId) outputModule = kv.second;
						if (kv.first == inputModuleId) inputModule = kv.second;
					}
					if (!outputModule || !inputModule) continue;

					engine::Cable* c = new engine::Cable;
					c->outputModule = outputModule->module;
					c->outputId = outputId;
					c->inputModule = inputModule->module;
					c->inputId = inputId;
					APP->engine->addCable(c);

					CableWidget* cw = new CableWidget;
					cw->setCable(c);
					if (colorStr) cw->color = color::fromHexString(colorStr);
					APP->scene->rack->addCable(cw);
				}
			}

			json_decref(rootJ);
		}

		// Bind created modules
		if (bindAfterCreate) {
			for (auto& kv : modules) {
				ModuleWidget* mw = kv.second;
				if (mw) {
					std::string s = module->bindModule(mw->module);
					if (!s.empty()) osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK, s.c_str());
				}
			}
		}
	}

	void showPlacementPreview() {
		if (!module || module->boundModules.empty() || !placerContainer) return;

		// Refresh stored positions from live widgets when available
		for (ReelModule::BoundModule* b : module->boundModules) {
			ModuleWidget* mw = b->getModuleWidget();
			if (mw) b->pos = (mw->box.pos - RACK_OFFSET) / RACK_GRID_SIZE;
		}

		// Compute relative positions from stored positions (works even when modules are absent)
		Vec minPos = Vec(std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity());
		for (ReelModule::BoundModule* b : module->boundModules) {
			minPos.x = std::min(minPos.x, b->pos.x);
			minPos.y = std::min(minPos.y, b->pos.y);
		}

		std::vector<std::pair<plugin::Model*, Vec>> modelPositions;
		std::vector<Vec> relativePositions;
		for (ReelModule::BoundModule* b : module->boundModules) {
			plugin::Model* model = plugin::getModel(b->pluginSlug, b->modelSlug);
			Vec relPos = b->pos.minus(minPos) * RACK_GRID_SIZE;
			modelPositions.push_back({model, relPos});
			relativePositions.push_back(relPos);
		}

		placerContainer->showPreview(modelPositions, 
			[=](Vec pos) {
				// Use unified importModulesAt for bound module recreation
				std::vector<std::pair<plugin::Model*, Vec>> moduleInfos;
				for (ReelModule::BoundModule* b : module->boundModules) {
					plugin::Model* model = plugin::getModel(b->pluginSlug, b->modelSlug);
					moduleInfos.push_back({model, Vec(0.f, 0.f)});
				}
				importModulesAt(pos, moduleInfos, relativePositions, false, NULL, NULL);
			}
		);
	}

	void importSelectionBind() {
		osdialog_filters* filters = osdialog_filters_parse(SELECTION_FILTERS);
		DEFER({osdialog_filters_free(filters);});

		char* path = osdialog_file(OSDIALOG_OPEN, "", NULL, filters);
		if (!path) return;
		DEFER({std::free(path);});

		// Load selection file
		FILE* file = std::fopen(path, "r");
		if (!file) return;
		DEFER({std::fclose(file);});

		json_error_t error;
		json_t* rootJ = json_loadf(file, 0, &error);
		if (!rootJ) {
			osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK, string::f("File is not a valid selection file. JSON parsing error at %s %d:%d %s", error.source, error.line, error.column, error.text).c_str());
			return;
		}
		// Increment ref count - will be decremented after user clicks or cancels
		json_incref(rootJ);

		// Check for unavailable modules and collect module data for preview
		std::set<std::string> pluginModuleSlugs;
		std::vector<std::pair<plugin::Model*, Vec>> modelPositions;
		std::vector<json_t*> moduleJsonList;

		double minX = std::numeric_limits<double>::infinity();
		double minY = std::numeric_limits<double>::infinity();

		json_t* modulesJ = json_object_get(rootJ, "modules");
		if (modulesJ) {
			json_t* moduleJ;
			size_t moduleIndex;
			json_array_foreach(modulesJ, moduleIndex, moduleJ) {
				// Check if plugin/model available
				json_t* pluginSlugJ = json_object_get(moduleJ, "plugin");
				json_t* modelSlugJ = json_object_get(moduleJ, "model");
				if (!pluginSlugJ || !modelSlugJ) continue;

				std::string pluginSlug = json_string_value(pluginSlugJ);
				std::string modelSlug = json_string_value(modelSlugJ);
				try {
					plugin::Model* model = plugin::modelFromJson(moduleJ);
					modelPositions.push_back({model, Vec(0.f, 0.f)}); // placeholder
				} catch (Exception& e) {
					pluginModuleSlugs.insert(pluginSlug + "/" + modelSlug);
					modelPositions.push_back({nullptr, Vec(0.f, 0.f)});
				}
				moduleJsonList.push_back(moduleJ);

				// Compute min position for relative placement
				json_t* posJ = json_object_get(moduleJ, "pos");
				double x = 0.0, y = 0.0;
				json_unpack(posJ, "[F, F]", &x, &y);
				minX = std::min(minX, x);
				minY = std::min(minY, y);
			}

			// Compute relative positions for preview
			for (size_t i = 0; i < moduleJsonList.size(); i++) {
				json_t* posJ = json_object_get(moduleJsonList[i], "pos");
				double x = 0.0, y = 0.0;
				json_unpack(posJ, "[F, F]", &x, &y);
				Vec relPos = Vec(x, y).minus(Vec(minX, minY)) * RACK_GRID_SIZE;
				if (i < modelPositions.size()) modelPositions[i].second = relPos;
			}
		}

		if (!pluginModuleSlugs.empty()) {
			std::string msg = "This selection includes modules that are not installed. Show missing modules on the VCV Library?";
			if (osdialog_message(OSDIALOG_WARNING, OSDIALOG_YES_NO, msg.c_str())) {
				std::string url = "https://library.vcvrack.com/?modules=";
				url += string::join(pluginModuleSlugs, ",");
				system::openBrowser(url);
			}
		}

		// Show preview and set up callback for actual import
		APP->scene->rack->deselectAll();

		// Extract relative positions from modelPositions (second element of each pair)
		std::vector<Vec> relativePositions;
		for (auto& mp : modelPositions) {
			relativePositions.push_back(mp.second);
		}

		// Build moduleInfos with just models (empty positions since we use relativePositions)
		std::vector<std::pair<plugin::Model*, Vec>> moduleInfos;
		for (auto& mp : modelPositions) {
			moduleInfos.push_back({mp.first, Vec(0.f, 0.f)});
		}

		auto callback = [=](Vec pos) {
			// Clear slots when importing a selection
			module->slots.clear();
			module->clearBoundModules();
			// Use unified importModulesAt for selection import
			importModulesAt(pos, moduleInfos, relativePositions, true, rootJ, NULL);
		};
		placerContainer->showPreview(modelPositions, callback);
	}

	void appendContextMenu(Menu* menu) override {
		ThemedModuleWidget<ReelModule>::appendContextMenu(menu);
		ReelModule* module = dynamic_cast<ReelModule*>(this->module);
		assert(module);

		menu->addChild(new MenuSeparator);
		menu->addChild(createBoolPtrMenuItem("Show module outlines", RACK_MOD_SHIFT_NAME "+B", &module->boxDraw));
		menu->addChild(Rack::createColorSubmenuItem("Outline color", &module->boxColor));

		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuItem("Recreate bound modules", "",
			[=]() { showPlacementPreview(); },
			module->boundModules.empty()
		));
		menu->addChild(createMenuItem("Bind from selection (.vcvs)", "", [=]() {
			importSelectionBind();
		}));

		menu->addChild(new MenuSeparator);
		bool noBound = !module || module->boundModules.empty();
		menu->addChild(createMenuItem("Randomize modules and cables", "", [=]() {
			module->slotRandomize(true, true);
		}, noBound));
		menu->addChild(createMenuItem("Randomize modules", "", [=]() {
			module->slotRandomize(true, false);
		}, noBound));
		menu->addChild(createMenuItem("Randomize cables", "", [=]() {
			module->slotRandomize(false, true);
		}, noBound));

		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuItem("Bind module (left expander)", "", [=]() {
			if (module->leftExpander.moduleId < 0) return;
			std::string s = module->bindModule(module->leftExpander.module);
			if (!s.empty()) osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK, s.c_str());
		}));
		menu->addChild(createMenuItem("Bind module (select one)", "", [=]() {
			moduleSelectProcessor.setOwner(this);
			moduleSelectProcessor.startLearn([=](ModuleWidget* mw, Vec pos) {
				std::string s = module->bindModule(mw->module);
				if (!s.empty()) osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK, s.c_str());
			});
		}));
		menu->addChild(createMenuItem("Bind modules (select multiple)", "", [=]() {
			moduleSelectProcessor.setOwner(this);
			moduleSelectProcessorStr = "";
			moduleSelectProcessor.startLearn(
				[=](ModuleWidget* mw, Vec pos) {
					std::string s = module->bindModule(mw->module);
					if (!s.empty()) moduleSelectProcessorStr += s + "\n";
				},
				ModuleSelectProcessor::LEARN_MODE::MULTI,
				[=]() {
					if (!moduleSelectProcessorStr.empty())
						osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK, moduleSelectProcessorStr.c_str());
				}
			);
		}));
		menu->addChild(createMenuItem("Bind modules (current selection)", "", [=]() {
			std::string s;
			for (ModuleWidget* mw : APP->scene->rack->getSelected()) {
				std::string _s = module->bindModule(mw->module);
				if (!_s.empty()) s += _s + "\n";
			}
			if (!s.empty()) osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK, s.c_str());
			APP->scene->rack->deselectAll();
		}));

		if (!module->boundModules.empty()) {
			menu->addChild(new MenuSeparator);
			menu->addChild(createSubmenuItem("Bound modules",
				string::f("%d", (int)module->boundModules.size()),
				[=](Menu* menu) {
					for (auto* b : module->boundModules) {
						ModuleWidget* mw = b->getModuleWidget();
						std::string text = (!mw ? "[ERROR] " : "") + b->moduleName;
						menu->addChild(createSubmenuItem(text, "", [=](Menu* menu) {
							ModuleWidget* mw2 = b->getModuleWidget();
							if (mw2) {
								menu->addChild(createMenuItem("Zoom to module", "", [=]() {
									StoermelderPackOne::Rack::ViewportCenter{mw2};
								}));
							}
							menu->addChild(createMenuItem("Unbind", "", [=]() {
								module->unbindModule(b);
							}));
						}));
					}
				}
			));
		}
	}

	void onHoverKey(const event::HoverKey& e) override {
		if (e.action == GLFW_PRESS && (e.mods & RACK_MOD_MASK) == GLFW_MOD_SHIFT) {
			if (e.key == GLFW_KEY_B && module) {
				module->boxDraw ^= true;
				e.consume(this);
			}
		}
		ModuleWidget::onHoverKey(e);
	}
};

} // namespace Reel
} // namespace StoermelderPackOne

Model* modelReel = createModel<StoermelderPackOne::Reel::ReelModule, StoermelderPackOne::Reel::ReelWidget>("Reel");