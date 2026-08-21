#pragma once
#include "../../plugin.hpp"
#include "MidiCat.hpp"
#include "MidiCat.slot.hpp"

namespace StoermelderPackOne {
namespace MidiCat {

struct MemStore;

/** Discovery and caching of MIDI-CAT's four right-side expanders.
 *
 *  The expanders may appear in any order and any subset, but at most one of each kind is
 *  used: the chain is walked to the right until an unrecognized module or a second
 *  expander of an already-claimed kind is found.
 *
 *  Written from the DSP thread in process() when the module graph changes; read from both
 *  the DSP and UI threads, hence the atomics.
 */
struct ExpanderSet {
	std::atomic<MidiCatMemBase*> expMem{NULL};
	std::atomic<MidiCatCtxBase*> expCtx{NULL};
	std::atomic<Module*> expClk{NULL};
	std::atomic<MidiCatFineBase*> expFine{NULL};

	MidiCatMemBase* mem() const {
		return expMem.load();
	}
	MidiCatCtxBase* ctx() const {
		return expCtx.load();
	}
	Module* clk() const {
		return expClk.load();
	}
	MidiCatFineBase* fine() const {
		return expFine.load();
	}

	/** The MEM-expander's stored mappings. Returns an invalid store -- on which every
	 *  operation is a no-op -- when no MEM-expander is attached.
	 *  Called from the UI thread. Defined below, after MemStore. */
	inline MemStore memStore() const;

	/** Re-walk the right-expander chain. Returns true for each kind that was still
	 *  attached before this scan but is not any more, so the caller can run the
	 *  detach clean-up that belongs to it. */
	struct Detached {
		bool clk = false;
		bool fine = false;
	};

	Detached scan(Module* self) {
		bool memFound = false, ctxFound = false, clkFound = false, fineFound = false;

		Module* exp = self->rightExpander.module;
		for (int i = 0; i < 4; i++) {
			if (!exp) break;
			if (exp->model == modelMidiCatMem && !memFound) {
				expMem.store(dynamic_cast<MidiCatMemBase*>(exp));
				memFound = true;
				exp = exp->rightExpander.module;
				continue;
			}
			if (exp->model == modelMidiCatCtx && !ctxFound) {
				expCtx.store(dynamic_cast<MidiCatCtxBase*>(exp));
				ctxFound = true;
				exp = exp->rightExpander.module;
				continue;
			}
			if (exp->model == modelMidiCatClk && !clkFound) {
				expClk.store(exp);
				clkFound = true;
				exp = exp->rightExpander.module;
				continue;
			}
			if (exp->model == modelMidiCatFine && !fineFound) {
				expFine.store(dynamic_cast<MidiCatFineBase*>(exp));
				fineFound = true;
				exp = exp->rightExpander.module;
				continue;
			}
			break;
		}

		Detached detached;
		if (!memFound) {
			expMem.store(NULL);
		}
		if (!ctxFound) {
			expCtx.store(NULL);
		}
		if (!clkFound) {
			detached.clk = expClk.load() != NULL;
			expClk.store(NULL);
		}
		if (!fineFound) {
			detached.fine = expFine.load() != NULL;
			expFine.store(NULL);
		}
		return detached;
	}
}; // struct ExpanderSet


/** Drives the CLK-expander's four clock inputs.
 *
 *  Each input is a trigger that releases the slots quantized to it: a slot in a
 *  clock-quantized mode holds its pending value until its clock source ticks.
 *  Owned by the DSP thread.
 */
struct ClkExpanderDriver {
	dsp::SchmittTrigger triggers[4];

	void process(Module* expClk, MappingSlot* slots, int numSlots) {
		for (int i = 0; i < 4; i++) {
			if (triggers[i].process(expClk->inputs[i].getVoltage())) {
				for (int j = 0; j < numSlots; j++) {
					slots[j].param.tick(i);
				}
			}
		}
	}
}; // struct ClkExpanderDriver


/** Drives the FINE-expander's two range gates.
 *
 *  The two gates select a precision for relative parameter adjustment; the high-range
 *  gate takes priority while both are held, and releasing it falls back to the low range
 *  if that one is still held.
 *  Owned by the DSP thread.
 */
struct FineExpanderDriver {
	dsp::SchmittTrigger lowTrigger;
	dsp::SchmittTrigger highTrigger;

	/** What the gates are asking for this sample. */
	struct Request {
		bool changed = false;
		bool enabled = false;
		float precision = 0.f;
		bool updateRefPoint = false;
	};

	Request process(MidiCatFineBase* expFine) {
		auto e1 = lowTrigger.processEvent(expFine->getLowRangeVoltage());
		auto e2 = highTrigger.processEvent(expFine->getHighRangeVoltage());

		Request r;
		if (e1 == dsp::SchmittTrigger::TRIGGERED && !highTrigger.isHigh()) {
			r.changed = true; r.enabled = true; r.precision = expFine->getLowRange();
		}
		if (e1 == dsp::SchmittTrigger::UNTRIGGERED && !highTrigger.isHigh()) {
			r.changed = true; r.enabled = false; r.precision = 0.f; r.updateRefPoint = false;
		}
		if (e2 == dsp::SchmittTrigger::TRIGGERED) {
			r.changed = true; r.enabled = true; r.precision = expFine->getHighRange();
			r.updateRefPoint = lowTrigger.isHigh();
		}
		if (e2 == dsp::SchmittTrigger::UNTRIGGERED) {
			r.changed = true;
			if (lowTrigger.isHigh()) {
				r.enabled = true; r.precision = expFine->getLowRange(); r.updateRefPoint = true;
			}
			else {
				r.enabled = false; r.precision = 0.f; r.updateRefPoint = false;
			}
		}
		return r;
	}
}; // struct FineExpanderDriver


/** The MEM-expander's stored mappings, keyed by (plugin slug, module slug).
 *
 *  A thin facade over the storage the expander module owns, so that the CRUD operations
 *  live in one place rather than interleaved with the DSP-thread code on MidiCatModule.
 *  Every method here runs on the UI thread.
 */
struct MemStore {
	MidiCatMemBase* expMem;

	MemStore(MidiCatMemBase* expMem) : expMem(expMem) {}

	typedef std::pair<std::string, std::string> Key;

	static Key keyOf(Module* m) {
		return Key(m->model->plugin->slug, m->model->slug);
	}

	/** False when no MEM-expander is attached. Every other method is a no-op in that
	 *  case, so callers only need this when the distinction matters to them. */
	bool valid() const {
		return expMem != NULL;
	}

	/** The stored mapping for the given model, or null. */
	MemModule* find(const Key& key) {
		if (!valid()) return NULL;
		auto storage = expMem->getMemStorage();
		auto it = storage->find(key);
		return it == storage->end() ? NULL : it->second;
	}

	/** Store a mapping, replacing and freeing any previous one for the same model. */
	void store(const Key& key, MemModule* m) {
		if (!valid()) { delete m; return; }
		auto storage = expMem->getMemStorage();
		auto it = storage->find(key);
		if (it != storage->end()) {
			delete it->second;
		}
		(*storage)[key] = m;
	}

	void erase(const Key& key) {
		if (!valid()) return;
		auto storage = expMem->getMemStorage();
		auto it = storage->find(key);
		if (it == storage->end()) return;
		delete it->second;
		storage->erase(key);
	}

	/** Capture every slot bound to the given model into storage, replacing any mapping
	 *  previously stored for it. `handles` supplies the parameter binding that the slots
	 *  themselves do not own.
	 *  Note the concrete ParamHandleIndicator type: the array is of the derived type, so
	 *  indexing it through a ParamHandle* would stride by the wrong size. */
	void save(const Key& key, MappingSlot* slots, ParamHandleIndicator* handles, int numSlots) {
		if (!valid()) return;
		MemModule* m = new MemModule;
		Module* module = NULL;
		for (int i = 0; i < numSlots; i++) {
			if (handles[i].moduleId < 0) continue;
			if (handles[i].module->model->plugin->slug != key.first && handles[i].module->model->slug == key.second) continue;
			module = handles[i].module;
			m->paramMap.push_back(slots[i].toMemParam(handles[i].paramId));
		}
		m->pluginName = module->model->plugin->name;
		m->moduleName = module->model->name;
		store(key, m);
	}

	/** True if a mapping exists for the module and the module passes the restriction
	 *  filter, if one is set. */
	bool test(Module* m) {
		if (!m || !valid()) return false;
		if (!find(keyOf(m))) return false;
		auto restriction = expMem->getMemModuleRestriction();
		if (restriction->size() > 0) {
			if (restriction->find(m->getId()) == restriction->end()) return false;
		}
		return true;
	}
}; // struct MemStore

inline MemStore ExpanderSet::memStore() const {
	return MemStore(mem());
}

} // namespace MidiCat
} // namespace StoermelderPackOne
