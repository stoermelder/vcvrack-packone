#pragma once
#include "../../tutorial/Tutorial.hpp"
#include <algorithm>
#include <cmath>


namespace StoermelderPackOne {
namespace SpliceKit {

namespace tutorial_detail {

// ---- Whole-tutorial snapshot/restore -------------------------------------------------------

// withModuleSnapshot()'s restore only calls dataFromJson(), which doesn't recreate cables (no
// patch load happens here) — so reconcile the live cables against the just-restored bitmask, and
// cancel any port-learn left active.
inline void reconcileAfterRestore(app::ModuleWidget* mw) {
	auto* module = dynamic_cast<SpliceKitModule*>(mw->module);
	if (!module) return;
	module->disablePortLearn();
	SceneConns target = module->sceneStore.connections[module->sceneStore.current];
	module->sceneStore.reconcile(module->sceneStore.current, target);
}

// ---- "Assign a port" step: a real, single-cell port-learn gesture -------------------------

// Puts the module into real single-cell learn state, same as "Learn" in the cell's context menu.
inline void beginPortLearnDemo(app::ModuleWidget* mw, int cellId) {
	auto* module = dynamic_cast<SpliceKitModule*>(mw->module);
	if (module) module->enablePortLearn(cellId, mw);
}

inline void endPortLearnDemo(app::ModuleWidget* mw) {
	auto* module = dynamic_cast<SpliceKitModule*>(mw->module);
	if (module) module->disablePortLearn();
}

// ---- Shared: a throwaway demo module next to SPLICE-KIT ------------------------------------

// Spawns a throwaway Fundamental "8vert" for a step to wire demo cables to — a real, commonly
// available module rather than touching whatever's already in the user's patch. 8vert always has
// a free in/out pair at index 0. Returns -1 if Fundamental isn't installed; callers no-op on that.
//
// Spawned first in an empty row above mw (its width isn't known until the panel loads), then
// re-anchored to mw's immediate left via setModuleWidgetPosNearest() — not setModuleWidgetPos(),
// which would shove mw itself aside. mw must never move. Placed left, not right, since the
// tutorial bubble prefers the right side.
inline int64_t spawnDemoModule(app::ModuleWidget* mw) {
	Vec spawnPos = mw->box.pos.minus(Vec(0.f, RACK_GRID_HEIGHT));
	int64_t id = vcv::addModule({"Fundamental", "8vert"}, spawnPos);
	if (id < 0) return id;
	if (ModuleWidget* demo = vcv::getModuleWidget(id)) {
		Vec pos = mw->box.pos.minus(Vec(demo->box.size.x, 0.f));
		vcv::setModuleWidgetPosNearest(id, pos);
	}
	return id;
}

inline void removeDemoModule(int64_t moduleId) {
	if (moduleId >= 0) vcv::removeModule(moduleId);
}

// The demo module's first `count` outputs and first `count` inputs, in port order. Either list
// is shorter than `count` if the demo module runs out; both empty if demoModuleId doesn't resolve.
inline std::pair<std::vector<PortAssignment>, std::vector<PortAssignment>> demoModuleOutputsInputs(int64_t demoModuleId, int count) {
	ModuleWidget* demo = demoModuleId >= 0 ? vcv::getModuleWidget(demoModuleId) : nullptr;
	if (!demo) return {};

	std::vector<PortAssignment> outs, ins;
	for (PortWidget* pw : demo->getPorts()) {
		if ((int) outs.size() >= count && (int) ins.size() >= count) break;
		PortAssignment pa;
		pa.moduleId = pw->module->getId();
		pa.portId = pw->portId;
		pa.type = pw->type;
		if (pa.type == engine::Port::OUTPUT && (int) outs.size() < count) outs.push_back(pa);
		else if (pa.type == engine::Port::INPUT && (int) ins.size() < count) ins.push_back(pa);
	}
	return {outs, ins};
}

// The demo module's first output/input pair. Both invalid if demoModuleId doesn't resolve.
inline std::pair<PortAssignment, PortAssignment> demoModuleOutIn(int64_t demoModuleId) {
	auto outsIns = demoModuleOutputsInputs(demoModuleId, 1);
	PortAssignment out = outsIns.first.empty() ? PortAssignment() : outsIns.first[0];
	PortAssignment in = outsIns.second.empty() ? PortAssignment() : outsIns.second[0];
	return {out, in};
}

// ---- "Patch" step: a live demo through the real module functions --------------------------

// Finds two free cells and assigns them to the demo module's first input/output pair via
// assignPort(), the same call a real Learn gesture drives. No-op (both -1) if there's no room.
inline std::pair<int, int> setupPatchDemo(app::ModuleWidget* mw, int64_t demoModuleId) {
	auto* module = dynamic_cast<SpliceKitModule*>(mw->module);
	if (!module || demoModuleId < 0) return {-1, -1};

	int freeA = -1, freeB = -1;
	for (int i = 0; i < MATRIX_COUNT; i++) {
		if (module->portAssignments[i].isValid()) continue;
		if (freeA < 0) freeA = i;
		else { freeB = i; break; }
	}
	if (freeA < 0 || freeB < 0) return {-1, -1}; // no two free cells to demo with

	auto outIn = demoModuleOutIn(demoModuleId);
	if (!outIn.first.isValid() || !outIn.second.isValid()) return {-1, -1};

	module->assignPort(freeA, outIn.first.moduleId, outIn.first.portId, outIn.first.type);
	module->assignPort(freeB, outIn.second.moduleId, outIn.second.portId, outIn.second.type);
	return {freeA, freeB};
}

// Illustrates one half of the real two-click patch gesture: first click arms pendingCellId (the
// cell's LED starts blinking), second click (a different cell) disarms it and toggles the
// connection. Can't call triggerCell() itself (asserts the engine thread) so its two visible
// effects are driven directly: pendingCellId for the arm/disarm, toggleConnection() for the cable.
inline void patchClick(SpliceKitModule* module, int cellId) {
	if (!module) return;
	if (module->pendingCellId < 0) {
		module->pendingCellId = cellId;
	}
	else if (module->pendingCellId == cellId) {
		module->clearPendingLocal();
	}
	else {
		int other = module->pendingCellId;
		module->clearPendingLocal();
		module->toggleConnection(other, cellId);
	}
}

// ---- "Scenes" step: a live demo through the real module functions -------------------------

// Finds up to two output/input pairs on the demo module and cross-wires them differently between
// the current scene ("baseScene") and its neighbour ("otherScene"), so switching scenes visibly
// rewires two cables instead of just one. Falls back to a single pair if only two free cells/
// ports are available, or a no-op if even that doesn't fit. Leaves the module back on baseScene.
struct ScenesDemoCells { int outA = -1, inA = -1, outB = -1, inB = -1; };
inline ScenesDemoCells setupScenesDemo(app::ModuleWidget* mw, int64_t demoModuleId) {
	auto* module = dynamic_cast<SpliceKitModule*>(mw->module);
	if (!module || demoModuleId < 0) return {};

	int free[4] = {-1, -1, -1, -1};
	int freeCount = 0;
	for (int i = 0; i < MATRIX_COUNT && freeCount < 4; i++) {
		if (module->portAssignments[i].isValid()) continue;
		free[freeCount++] = i;
	}
	if (freeCount < 2) return {}; // not even a single pair fits

	auto outsIns = demoModuleOutputsInputs(demoModuleId, 2);
	int portPairs = std::min((int) outsIns.first.size(), (int) outsIns.second.size());
	if (portPairs < 1) return {};

	bool twoPairs = freeCount >= 4 && portPairs >= 2;

	ScenesDemoCells cells;
	cells.outA = free[0];
	cells.inA = free[1];
	module->assignPort(cells.outA, outsIns.first[0].moduleId, outsIns.first[0].portId, outsIns.first[0].type);
	module->assignPort(cells.inA, outsIns.second[0].moduleId, outsIns.second[0].portId, outsIns.second[0].type);

	if (!twoPairs) {
		module->toggleConnection(cells.outA, cells.inA); // connects on the current (base) scene only
		return cells;
	}

	cells.outB = free[2];
	cells.inB = free[3];
	module->assignPort(cells.outB, outsIns.first[1].moduleId, outsIns.first[1].portId, outsIns.first[1].type);
	module->assignPort(cells.inB, outsIns.second[1].moduleId, outsIns.second[1].portId, outsIns.second[1].type);

	int baseScene = module->sceneStore.current;
	int otherScene = (baseScene + 1) % SCENE_COUNT;

	module->toggleConnection(cells.outA, cells.inA);
	module->toggleConnection(cells.outB, cells.inB);

	module->sceneStore.switchTo(otherScene);
	module->toggleConnection(cells.outA, cells.inB);

	module->sceneStore.switchTo(baseScene);
	return cells;
}

// ---- "Port map view" step: the module's real port-map overlay, turned on for the step -------

// Turns the real port-map overlay on/off, same as SPACE.
inline void beginVizModeDemo(app::ModuleWidget* mw) {
	setSpliceKitVizMode(mw, true);
}

inline void endVizModeDemo(app::ModuleWidget* mw) {
	setSpliceKitVizMode(mw, false);
}

// Assigns up to 4 free cells to distinct demo-module ports (no cabling needed — the overlay
// draws one spline per assigned cell) so the port-map view has something to show.
inline std::vector<int> setupPortMapDemo(app::ModuleWidget* mw, int64_t demoModuleId) {
	static constexpr int PORT_MAP_DEMO_CELLS = 4;

	auto* module = dynamic_cast<SpliceKitModule*>(mw->module);
	ModuleWidget* demo = demoModuleId >= 0 ? vcv::getModuleWidget(demoModuleId) : nullptr;
	if (!module || !demo) return {};

	std::vector<int> cells;
	auto ports = demo->getPorts();
	size_t portIdx = 0;
	for (int i = 0; i < MATRIX_COUNT && (int) cells.size() < PORT_MAP_DEMO_CELLS && portIdx < ports.size(); i++) {
		if (module->portAssignments[i].isValid()) continue;
		PortWidget* pw = ports[portIdx++];
		module->assignPort(i, pw->module->getId(), pw->portId, pw->type);
		cells.push_back(i);
	}
	return cells;
}

// Adds a widget of type W below mw's tutorial overlay, so its drawLayer(1) content sits under
// the overlay's dim/cutout paint.
template <typename W>
inline W* addStepWidget(app::ModuleWidget* mw) {
	auto* w = new W;
	Widget* overlay = mw->children.empty() ? nullptr : mw->children.back();
	if (overlay) mw->addChildBelow(w, overlay);
	else mw->addChild(w);
	return w;
}

template <typename W>
inline void removeStepWidget(app::ModuleWidget* mw) {
	for (Widget* w : mw->children) {
		if (dynamic_cast<W*>(w)) {
			mw->removeChild(w);
			delete w;
			return;
		}
	}
}

} // namespace tutorial_detail

// mw is bound only for the onOpen/onClose/onEnter/onLeave closures below, which drive the demo
// through SpliceKitModule's own public functions. withModuleSnapshot() snapshots and restores the
// module once, bracketing every step (with a reset on open), so nothing any step does survives it.
inline Tutorial::Tutorial spliceKitTutorial(app::ModuleWidget* mw) {
	using Tutorial::Target;
	using Tutorial::Step;
	Tutorial::Tutorial t;
	t.title = "SPLICE-KIT";

	// withModuleSnapshot() runs the restore before this, so reconcileAfterRestore() diffs against
	// the just-restored bitmask, not the demo's still-live state.
	t.onClose = [mw]() { tutorial_detail::reconcileAfterRestore(mw); };
	Tutorial::withModuleSnapshot(mw, t, /* resetOnOpen */ true);

	Tutorial::addStep(t,
		"Welcome",
		"An 8×8 patch bay: every button stands for one port in the patch, and pressing two "
		"buttons creates or removes a cable."
	);

	Tutorial::addStep(t,
		"The matrix",
		"64 buttons, each can be assigned to any input or output in the patch.",
		Target::params(SpliceKitModule::PARAM_MATRIX, MATRIX_COUNT)
	);

	// Opens cell 0's actual right-click menu instead of only narrating it.
	Tutorial::addStep(t,
		"Cell menu",
		"Every cell has its context menu: rename it, pick a color, remove "
		"just its cables, or start port/MIDI learn from there instead.\n\n"
		"\"Learn\" assigns just this cell. \"Start sequential learn...\" assigns this cell, then "
		"keeps advancing to the next one after each click — so you can walk through "
		"many buttons in a row without reopening the menu each time.",
		Target::param(SpliceKitModule::PARAM_MATRIX + 0),
		[mw](Step& step) {
			auto preview = std::make_shared<Tutorial::MenuPreview>();
			step.onEnter = [mw, preview]() {
				*preview = Tutorial::openMenuPreview(mw, [mw]() {
					auto* module = dynamic_cast<SpliceKitModule*>(mw->module);
					if (module) openSpliceKitCellMenu(module, mw, 0);
				}, [](ui::Menu* menu) {
					return Tutorial::findMenuItemsByLabel(menu, {"Module port", "Learn", "Start sequential learn..."});
				});
			};
			step.onLeave = [preview]() {
				Tutorial::closeMenuPreview(*preview);
			};
		}
	);

	// Starts real single-cell port-learn on cell 0 so clicking any port assigns it live.
	Tutorial::addStep(t,
		"Assign a port",
		"That \"Learn\" from the cell menu isn't the only way in: you can also just drop a "
		"dragged cable onto the button.\n\n"
		"Cell 1 is in learn mode right now — click any port in the patch to see it assigned.",
		Target::param(SpliceKitModule::PARAM_MATRIX + 0),
		[mw](Step& step) {
			step.onEnter = [mw]() {
				tutorial_detail::beginPortLearnDemo(mw, 0);
			};
			step.onLeave = [mw]() {
				tutorial_detail::endPortLearnDemo(mw);
			};
		}
	);

	// Spawns a demo module, wires two free cells to it, and shows a fake cursor clicking both in
	// turn via patchClick(). Demo module is removed again on leave.
	Tutorial::addStep(t,
		"Patch",
		"Pressing two assigned buttons creates a cable between their ports; pressing them "
		"again removes it. You can only patch cables between an input and an output.\n\nBy "
		"default assigned cells are colored to tell them apart: red for an output, blue for "
		"an input (each cell's color can be changed in its own context menu).",
		Target::params(SpliceKitModule::PARAM_MATRIX, MATRIX_COUNT),
		[mw](Step& step) {
			auto demoModuleId = std::make_shared<int64_t>(-1);
			auto demoCells = std::make_shared<std::pair<int, int>>(-1, -1);
			step.onEnter = [mw, demoModuleId, demoCells]() {
				auto* module = dynamic_cast<SpliceKitModule*>(mw->module);
				if (!module) return;
				*demoModuleId = tutorial_detail::spawnDemoModule(mw);
				*demoCells = tutorial_detail::setupPatchDemo(mw, *demoModuleId);
				int cellA = demoCells->first, cellB = demoCells->second;
				auto* cursor = tutorial_detail::addStepWidget<Tutorial::CursorDemoWidget>(mw);
				cursor->mw = mw;
				if (cellA >= 0 && cellB >= 0) {
					cursor->stops = {
						Tutorial::cursorDemoParamStop(SpliceKitModule::PARAM_MATRIX + cellA, [module, cellA]() {
							tutorial_detail::patchClick(module, cellA);
						}),
						Tutorial::cursorDemoParamStop(SpliceKitModule::PARAM_MATRIX + cellB, [module, cellB]() {
							tutorial_detail::patchClick(module, cellB);
						})
					};
				}
			};
			step.onLeave = [mw, demoModuleId, demoCells]() {
				tutorial_detail::removeStepWidget<Tutorial::CursorDemoWidget>(mw);
				// Drop any mid-cycle pending state so the cursor can't strand a cell blinking.
				auto* module = dynamic_cast<SpliceKitModule*>(mw->module);
				if (module) {
					module->clearPendingLocal();
					// clearPort() also cleans up the connection/bitmask, in case the cursor never
					// reached its second click.
					if (demoCells->first >= 0) module->clearPort(demoCells->first);
					if (demoCells->second >= 0) module->clearPort(demoCells->second);
				}
				tutorial_detail::removeDemoModule(*demoModuleId);
				*demoModuleId = -1;
				*demoCells = {-1, -1};
			};
		}
	);

	// Spawns a demo module (see setupScenesDemo()), then shows a fake cursor clicking back and
	// forth between the two scenes, calling the real switchTo() on each click.
	Tutorial::addStep(t,
		"Scenes",
		"Eight scenes store separate cable sets over the same assignments. If you switch a "
		"scene, cables are added and removed automatically.",
		Target::params(SpliceKitModule::PARAM_SCENE, SCENE_COUNT),
		[mw](Step& step) {
			auto demoModuleId = std::make_shared<int64_t>(-1);
			auto demoCells = std::make_shared<tutorial_detail::ScenesDemoCells>();
			step.onEnter = [mw, demoModuleId, demoCells]() {
				auto* module = dynamic_cast<SpliceKitModule*>(mw->module);
				if (!module) return;
				int baseScene = module->sceneStore.current;
				int otherScene = (baseScene + 1) % SCENE_COUNT;
				*demoModuleId = tutorial_detail::spawnDemoModule(mw);
				*demoCells = tutorial_detail::setupScenesDemo(mw, *demoModuleId);
				auto* cursor = tutorial_detail::addStepWidget<Tutorial::CursorDemoWidget>(mw);
				cursor->mw = mw;
				cursor->stops = {
					Tutorial::cursorDemoParamStop(SpliceKitModule::PARAM_SCENE + baseScene, [module, baseScene]() {
						module->sceneStore.switchTo(baseScene);
					}),
					Tutorial::cursorDemoParamStop(SpliceKitModule::PARAM_SCENE + otherScene, [module, otherScene]() {
						module->sceneStore.switchTo(otherScene);
					})
				};
			};
			step.onLeave = [mw, demoModuleId, demoCells]() {
				tutorial_detail::removeStepWidget<Tutorial::CursorDemoWidget>(mw);
				// clearPort() reaches every scene's stored bitmask, not just the active one.
				auto* module = dynamic_cast<SpliceKitModule*>(mw->module);
				if (module) {
					if (demoCells->outA >= 0) module->clearPort(demoCells->outA);
					if (demoCells->inA >= 0) module->clearPort(demoCells->inA);
					if (demoCells->outB >= 0) module->clearPort(demoCells->outB);
					if (demoCells->inB >= 0) module->clearPort(demoCells->inB);
				}
				tutorial_detail::removeDemoModule(*demoModuleId);
				*demoCells = {};
				*demoModuleId = -1;
			};
		}
	);

	// Spawns a demo module, assigns cells to its ports so the port-map overlay has splines to
	// show, and pops up a "SPACE" key cap illustrating the shortcut itself.
	Tutorial::addStep(t,
		"Port map view",
		"Hover the module and press SPACE to show lines from each cell to its port, like "
		"below. It works during the tutorial too.",
		Target::params(SpliceKitModule::PARAM_MATRIX, MATRIX_COUNT),
		[mw](Step& step) {
			auto demoModuleId = std::make_shared<int64_t>(-1);
			auto demoCells = std::make_shared<std::vector<int>>();
			step.onEnter = [mw, demoModuleId, demoCells]() {
				*demoModuleId = tutorial_detail::spawnDemoModule(mw);
				*demoCells = tutorial_detail::setupPortMapDemo(mw, *demoModuleId);
				tutorial_detail::beginVizModeDemo(mw);
				auto* cursor = tutorial_detail::addStepWidget<Tutorial::CursorDemoWidget>(mw);
				cursor->mw = mw;
				cursor->stops = { Tutorial::cursorDemoKeyStop("SPACE") };
			};
			step.onLeave = [mw, demoModuleId, demoCells]() {
				tutorial_detail::endVizModeDemo(mw);
				tutorial_detail::removeStepWidget<Tutorial::CursorDemoWidget>(mw);
				auto* module = dynamic_cast<SpliceKitModule*>(mw->module);
				if (module) {
					for (int cellId : *demoCells) module->clearPort(cellId);
				}
				tutorial_detail::removeDemoModule(*demoModuleId);
				demoCells->clear();
				*demoModuleId = -1;
			};
		}
	);

	// Opens the module's actual context menu, positioned next to the panel.
	Tutorial::addStep(t,
		"Context menu",
		"Using the context menu, you can assign a MIDI input/output pair to play and "
		"light up SPLICE-KIT from a grid controller.\n\n" 
		"Pick one of the built-in MIDI presets, or map individual "
		"buttons to specific notes. Port tools and scene options live here too.",
		Target::none(),
		[mw](Step& step) {
			auto preview = std::make_shared<Tutorial::MenuPreview>();
			step.onEnter = [mw, preview]() {
				*preview = Tutorial::openMenuPreview(mw, [mw]() {
					mw->createContextMenu();
				});
			};
			step.onLeave = [preview]() {
				Tutorial::closeMenuPreview(*preview);
			};
		}
	);

	// Opens cell 0's menu again, this time highlighting its MIDI section.
	Tutorial::addStep(t,
		"MIDI learn",
		"Every button can be MIDI-triggered and learned "
		"— the same \"Learn\" and \"Start sequential learn...\" pattern, just for MIDI.\n\n"
		"This only wires up input: lighting a button back up on your controller isn't "
		"per-button mappable, it comes from picking one \"MIDI Preset\" instead.",
		Target::param(SpliceKitModule::PARAM_MATRIX + 0),
		[mw](Step& step) {
			auto preview = std::make_shared<Tutorial::MenuPreview>();
			step.onEnter = [mw, preview]() {
				*preview = Tutorial::openMenuPreview(mw, [mw]() {
					auto* module = dynamic_cast<SpliceKitModule*>(mw->module);
					if (module) openSpliceKitCellMenu(module, mw, 0);
				}, [](ui::Menu* menu) {
					return Tutorial::findMenuItemsByLabel(menu, {"MIDI", "Learn", "Start sequential learn..."});
				});
			};
			step.onLeave = [preview]() {
				Tutorial::closeMenuPreview(*preview);
			};
		}
	);

	// Closing step: points to the online manual for anything the tutorial didn't cover.
	Tutorial::addStep(t,
		"Manual",
		"That's the basics. The manual has the full picture, including all MIDI feedback "
		"presets and scene options.",
		Target::none(),
		[mw](Step& step) {
			if (mw->model) step.linkUrl = mw->model->manualUrl;
			step.linkText = "Open manual";
		}
	);

	return t;
}

} // namespace SpliceKit
} // namespace StoermelderPackOne