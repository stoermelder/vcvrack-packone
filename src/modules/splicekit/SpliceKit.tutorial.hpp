#pragma once
#include "../../tutorial/Tutorial.hpp"


namespace StoermelderPackOne {
namespace SpliceKit {

// A plain factory (no ModuleWidget parameter): each Target resolves lazily against whatever
// widget TutorialOverlay::step() hands it, so the same Tutorial value works for every
// SPLICE-KIT instance.
inline Tutorial::Tutorial spliceKitTutorial() {
	using Tutorial::Target;
	using Tutorial::Step;
	Tutorial::Tutorial t;
	t.title = "SPLICE-KIT";

	t.steps.push_back(Step(
		"Welcome",
		"An 8×8 patch bay: every button stands for one port in the patch, and pressing two "
		"buttons creates or removes a cable. No CV in or out, by design."
	));

	t.steps.push_back(Step(
		"The matrix",
		"64 buttons, each can be assigned to any input or output in the patch.",
		Target::params(SpliceKitModule::PARAM_MATRIX, MATRIX_COUNT)
	));

	t.steps.push_back(Step(
		"Assign a port",
		"Right-click → Module port → Learn, then click a port. Or drop a dragged cable "
		"onto the button. Sequential learn for many buttons.",
		Target::param(SpliceKitModule::PARAM_MATRIX + 0)
	));

	t.steps.push_back(Step(
		"Patch",
		"Press two assigned buttons to create a cable between their ports; press them again to "
		"remove it.",
		Target::param(SpliceKitModule::PARAM_MATRIX + 1)
	));

	t.steps.push_back(Step(
		"Scenes",
		"Eight scenes store separate cable sets over the same assignments.",
		Target::params(SpliceKitModule::PARAM_SCENE, SCENE_COUNT)
	));

	t.steps.push_back(Step(
		"Port map view",
		"Hover the module and press SPACE to show lines from each cell to its port. It works "
		"during the tutorial.",
		Target::params(SpliceKitModule::PARAM_MATRIX, MATRIX_COUNT)
	));

	t.steps.push_back(Step(
		"MIDI",
		"Every button can be MIDI-triggered and learned; MIDI feedback presets for grid "
		"controllers are in the context menu. See the manual for more."
	));

	return t;
}

} // namespace SpliceKit
} // namespace StoermelderPackOne