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
	virtual IntermixMatrix expGetCurrentMatrix() { return NULL; }
	virtual int expGetChannelCount() { return 0; }
	virtual void expSetFade(int i, float* fadeIn, float* fadeOut) { }
};

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