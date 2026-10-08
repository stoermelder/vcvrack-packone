#pragma once
#include "../../plugin.hpp"
#include "../../components/LedTextDisplay.hpp"

namespace StoermelderPackOne {
namespace Intermix {

enum FADE_LENGTH {
	FADE_LENGTH_4S = 0,
	FADE_LENGTH_15S = 1,
	FADE_LENGTH_60S = 2
};

template<int PORTS>
struct IntermixBase {
	typedef float (*IntermixMatrix)[PORTS];
	virtual IntermixMatrix expGetCurrentMatrix() = 0;
	virtual int expGetChannelCount() { return 0; }
	virtual void expSetFade(int i, float* fadeIn, float* fadeOut) { }
};

inline bool isIntermixModel(Model* model) {
	return model == modelIntermix
		|| model == modelIntermixGate
		|| model == modelIntermixEnv
		|| model == modelIntermixFade;
}

/** Common base for all modules of the Intermix expander-chain (Intermix,
 * IntermixGate, IntermixEnv, IntermixFade).
 *
 * Chain members forward the IntermixBase* of the chain head through their own
 * rightExpander, so when any member is removed the members to its right keep
 * holding a stale forwarded pointer (Rack clears the expander module on
 * removal but not its messages). Each member therefore registers a
 * module-listener: on removal it stops publishing its own message and notifies
 * the surviving members, which drop the messages they forwarded themselves.
 * A changed left neighbor (ExpanderChangeEvent) unpublishes the forwarded
 * message immediately as well.
 */
struct IntermixChainModule : Module, ModuleChangeListener {
	IntermixChainModule() {
		moduleChangedFlag = false;
		registerModuleListener("Intermix", this);
	}

	~IntermixChainModule() {
		unregisterModuleListener("Intermix", this);
	}

#ifndef METAMODULE
	void onRemove(const Module::RemoveEvent& e) override {
		// Readers to the right consume rightExpander.consumerMessage directly,
		// so stop publishing our own message immediately.
		unpublishExpanderMessage();
		// Have the surviving chain members drop the messages they forwarded.
		notifyModuleListeners("Intermix");
		Module::onRemove(e);
	}
#endif

	void onExpanderChange(const Module::ExpanderChangeEvent& e) override {
		// Dispatched when the neighbor on the given side is removed, replaced or
		// the rack is rearranged; runs under the engine lock, on the audio thread
		// (block start) or the UI thread (module removal). A changed left
		// neighbor invalidates the message this module forwarded.
		if (e.side == 0) {
			unpublishExpanderMessage();
			resetOutputs();
		}
		Module::onExpanderChange(e);
	}

	/** Consumes a sibling-removal notification. Returns true if the caller must
	 * skip processing this sample because the message of its left expander may
	 * be stale; the pointers published by this module are cleared either way. */
	bool consumeSiblingRemoved() {
		if (!moduleChangedFlag) return false;
		moduleChangedFlag = false;
		unpublishExpanderMessage();
		return true;
	}

	/** Stops publishing the expander-message of this module, invalidating the
	 * pointer which readers to the right consume directly. */
	void unpublishExpanderMessage() {
		rightExpander.producerMessage = NULL;
		rightExpander.consumerMessage = NULL;
		rightExpander.messageFlipRequested = true;
	}

	/** Subclasses with outputs reset them here when the chain disconnects. */
	virtual void resetOutputs() { }
};


/** LED display showing MODULE::input (a 0-based row index into the chain
 * head's matrix) as a 1-based two-digit number, with a right-click context
 * menu to select it. Shared by IntermixEnv and IntermixFade, whose "input"
 * displays are otherwise identical.
 */
template<typename MODULE, int PORTS>
struct InputLedDisplay : StoermelderLedDisplay {
	MODULE* module;

	void step() override {
		if (module) {
			text = string::f("%02d", module->input + 1);
		}
		else {
			text = "";
		}
		StoermelderLedDisplay::step();
	}

	void onButton(const event::Button& e) override {
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_RIGHT) {
			createContextMenu();
			e.consume(this);
		}
		StoermelderLedDisplay::onButton(e);
	}

	void createContextMenu() {
		ui::Menu* menu = createMenu();
		menu->addChild(createMenuLabel("Input"));
		for (int i = 0; i < PORTS; i++) {
			menu->addChild(StoermelderPackOne::Rack::createValuePtrMenuItem(string::f("%02u", i + 1), &module->input, i));
		}
	}
};


template<typename MODULE>
struct FadeLengthParamQuantity : ParamQuantity {
	MODULE* module;

	float getMaxValue() override {
		switch (module->fadeLengthMode) {
			case FADE_LENGTH_4S: return 4.f;
			case FADE_LENGTH_15S: return 15.f;
			case FADE_LENGTH_60S: return 60.f;
		}
		return 0.f;
	}

	std::string getUnit() override {
		switch (module->fadeLengthMode) {
			case FADE_LENGTH_4S: return "s (4s max)";
			case FADE_LENGTH_15S: return "s (15s max)";
			case FADE_LENGTH_60S: return "s (60s max)";
		}
		return "";
	}
};

} // namespace Intermix
} // namespace StoermelderPackOne