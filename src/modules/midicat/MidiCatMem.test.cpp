#include "../../test/framework.hpp"
#include "MidiCatMem.cpp"
#include "MidiCat.cpp"
#include "../midi/MidiTrackingProcessor.hpp"

using namespace StoermelderPackOne::MidiCat;

SYNC_MODEL(modelMidiCat, "MidiCat");
SYNC_MODEL(modelMidiCatMem, "MidiCatEx");
Test::TestContext<> testContext;

// Helper: set up a CC+param binding on a given MidiCat channel.
// The target module must already be registered in the engine.
static void setupBinding(Test::Harness& h, MidiCatModule* midicat, Module* target, int channel, int cc, int paramId) {
	midicat->processDivider.setDivision(1);
	midicat->enableLearn(channel, true);
	midicat->midiInput.onMessage(Test::makeMidiMessage(0xb, 0, cc, 64));
	midicat->learnParam(channel, target->id, paramId);
	h.dspStep();
	midicat->slots[channel].cc.ccMode = CCMODE::DIRECT;
}

// Helper: build and insert a MemModule entry for `target` in mem->midiMap.
// The paramMap gets one entry: { paramId=pid, cc=cc }.
static void insertMemEntry(MidiCatMemModule* mem, Module* target, int pid, int cc) {
	auto* memMod = new MemModule;
	memMod->pluginName = target->model->plugin->name;
	memMod->moduleName = target->model->name;
	MemParam* p = new MemParam;
	p->paramId = pid;
	p->cc = cc;
	p->ccMode = CCMODE::DIRECT;
	memMod->paramMap.push_back(p);
	auto key = std::make_pair(target->model->plugin->slug, target->model->slug);
	mem->midiMap[key] = memMod;
}


TEST_CASE("Construction and initialization", "[MidiCatMem]") {
	Test::Harness h;
	MidiCatMemModule* m = h.addModule<MidiCatMemModule>("MidiCatEx");

	REQUIRE(m != nullptr);
	REQUIRE(m->NUM_PARAMS == 3);
	REQUIRE(m->NUM_INPUTS == 2);
	REQUIRE(m->NUM_OUTPUTS == 0);
	REQUIRE(m->NUM_LIGHTS == 1);
	REQUIRE(m->midiMap.empty());
	REQUIRE(m->moduleRestriction.empty());
}

TEST_CASE("Preset JSON null-guards", "[MidiCatMem][JSON]") {
	Test::Harness h;
	auto module = h.addModule<MidiCatMemModule>("MidiCatEx");

	SECTION("All top-level properties are null-guarded in dataFromJson()") {
		json_t* rootJ = module->dataToJson();
		REQUIRE(rootJ != nullptr);
		Test::testPresetNullGuards(module, rootJ);
		json_decref(rootJ);
	}

	SECTION("All properties tolerate wrong-typed values") {
		json_t* rootJ = module->dataToJson();
		REQUIRE(rootJ != nullptr);
		Test::testPresetTypeConfusion(module, rootJ);
		json_decref(rootJ);
	}

	SECTION("All arrays tolerate being oversized") {
		json_t* rootJ = module->dataToJson();
		REQUIRE(rootJ != nullptr);
		Test::testPresetOversizedArrays(module, rootJ);
		json_decref(rootJ);
	}

}

TEST_CASE("JSON round-trip preserves state", "[MidiCatMem]") {
	Test::Harness h;
	MidiCatMemModule* m = h.addModule<MidiCatMemModule>("MidiCatEx");

	m->panelTheme = 2;
	m->moduleRestriction.insert(99);

	// Add one MemModule entry with two params
	auto* mod = new MemModule;
	mod->pluginName = "PluginA";
	mod->moduleName = "ModA";
	MemParam* p1 = new MemParam; p1->paramId = 0; p1->cc = 7;  p1->min = 0.f; p1->max = 1.f;
	MemParam* p2 = new MemParam; p2->paramId = 1; p2->cc = 10; p2->min = 0.f; p2->max = 1.f;
	mod->paramMap = {p1, p2};
	m->midiMap[{"PluginA", "ModA"}] = mod;

	json_t* j = m->dataToJson();

	// Wipe and reload
	Module::ResetEvent re;
	m->onReset(re);
	m->panelTheme = 0;
	m->dataFromJson(j);
	json_decref(j);

	REQUIRE(m->panelTheme == 2);
	REQUIRE(m->moduleRestriction.count(99) == 1);
	REQUIRE(m->midiMap.size() == 1);

	auto it = m->midiMap.find({"PluginA", "ModA"});
	REQUIRE(it != m->midiMap.end());
	REQUIRE(it->second->pluginName == "PluginA");
	REQUIRE(it->second->moduleName == "ModA");
	REQUIRE(it->second->paramMap.size() == 2);

	auto pit = it->second->paramMap.begin();
	REQUIRE((*pit)->paramId == 0);
	REQUIRE((*pit)->cc == 7);
	++pit;
	REQUIRE((*pit)->paramId == 1);
	REQUIRE((*pit)->cc == 10);
}


// ─── Standalone tests ───────────────────────────────────────────────────────

TEST_CASE("process() publishes midiMap via leftExpander", "[MidiCatMem]") {
	Test::Harness h;
	MidiCatMemModule* m = h.addModule<MidiCatMemModule>("MidiCatEx");

	// Calling process() directly, not h.dspStep(): this asserts process()'s own effect
	// before any engine-style flip, and dspStep() would immediately flip
	// producer/consumer and clear messageFlipRequested, hiding exactly the state below.
	m->process(Test::makeProcessArgs(1));

	REQUIRE(m->leftExpander.producerMessage == &m->midiMap);
	REQUIRE(m->leftExpander.messageFlipRequested == true);
}

TEST_CASE("process() does not crash without left expander", "[MidiCatMem]") {
	Test::Harness h;
	MidiCatMemModule* m = h.addModule<MidiCatMemModule>("MidiCatEx");
	REQUIRE_NOTHROW(m->process(Test::makeProcessArgs(1)));
}

TEST_CASE("onReset clears midiMap and moduleRestriction", "[MidiCatMem]") {
	Test::Harness h;
	MidiCatMemModule* m = h.addModule<MidiCatMemModule>("MidiCatEx");

	// Insert a fake entry and a restriction
	auto* mod = new MemModule;
	m->midiMap[{"slug", "mod"}] = mod;
	m->moduleRestriction.insert(42);

	REQUIRE(!m->midiMap.empty());
	REQUIRE(!m->moduleRestriction.empty());

	Module::ResetEvent re;
	m->onReset(re);

	REQUIRE(m->midiMap.empty());
	REQUIRE(m->moduleRestriction.empty());
}

// ─── Integration tests ──────────────────────────────────────────────────────

TEST_CASE("MidiCat detects expander", "[MidiCatMem][MidiCat]") {
	Test::Harness h;
	MidiCatModule* midicat = h.addModule<MidiCatModule>("MidiCat");
	MidiCatMemModule* mem = h.addModule<MidiCatMemModule>("MidiCatEx");
	// MidiCat.expanders.hpp detects mem via `exp->model == modelMidiCatMem` — a mismatch here
	// (missing/wrong SYNC_MODEL) would make the REQUIRE below fail with no useful diagnosis.
	Test::requireModelSync(modelMidiCatMem, "MidiCatEx");

	// Flush initial expandersChanged so expMem is properly null before connecting
	h.dspStep();
	REQUIRE(midicat->expanders.mem() == nullptr);

	h.connectExpander(midicat, mem);
	h.dspStep();

	REQUIRE(midicat->expanders.mem() != nullptr);
	REQUIRE(midicat->expanders.mem() == dynamic_cast<MidiCatMemBase*>(mem));
}

TEST_CASE("Disconnecting expander clears expMem", "[MidiCatMem][MidiCat]") {
	Test::Harness h;
	MidiCatModule* midicat = h.addModule<MidiCatModule>("MidiCat");
	MidiCatMemModule* mem = h.addModule<MidiCatMemModule>("MidiCatEx");

	h.connectExpander(midicat, mem);
	h.dspStep();
	REQUIRE(midicat->expanders.mem() != nullptr);

	h.disconnectExpander(midicat, Test::Harness::SIDE_RIGHT);
	h.dspStep();

	REQUIRE(midicat->expanders.mem() == nullptr);
}

TEST_CASE("MemStore::test returns false for unknown module", "[MidiCatMem][MidiCat]") {
	Test::Harness h;
	MidiCatModule* midicat = h.addModule<MidiCatModule>("MidiCat");
	MidiCatMemModule* mem = h.addModule<MidiCatMemModule>("MidiCatEx");
	MidiCatMemModule* unknown = h.addModule<MidiCatMemModule>("MidiCatEx");

	h.connectExpander(midicat, mem);
	h.dspStep();
	// midiMap is empty, so no slug matches
	REQUIRE_FALSE(midicat->expanders.memStore().test(unknown));
}

TEST_CASE("MemStore::save stores current MidiCat CC mapping", "[MidiCatMem][MidiCat]") {
	Test::Harness h;
	MidiCatModule* midicat = h.addModule<MidiCatModule>("MidiCat");
	MidiCatMemModule* mem = h.addModule<MidiCatMemModule>("MidiCatEx");
	// Use a second MidiCatMemModule as target (it has parameters and a proper model)
	MidiCatMemModule* target = h.addModule<MidiCatMemModule>("MidiCatEx");

	// Bind CC 7 → target PARAM_APPLY (id=0)
	setupBinding(h, midicat, target, 0, 7, MidiCatMemModule::PARAM_APPLY);

	h.connectExpander(midicat, mem);
	h.dspStep();

	REQUIRE_FALSE(midicat->expanders.memStore().test(target));

	// Save current mapping for this target module type
	midicat->expanders.memStore().save(MemStore::Key(target->model->plugin->slug, target->model->slug), midicat->slots, midicat->paramHandles, MAX_CHANNELS);

	// The midiMap should now contain one entry for the target's slugs
	REQUIRE(midicat->expanders.memStore().test(target));
	REQUIRE(mem->midiMap.size() == 1);

	auto it = mem->midiMap.find({target->model->plugin->slug, target->model->slug});
	REQUIRE(it != mem->midiMap.end());
	REQUIRE(it->second->paramMap.size() == 1);
	REQUIRE(it->second->paramMap.front()->cc == 7);
	REQUIRE(it->second->paramMap.front()->paramId == MidiCatMemModule::PARAM_APPLY);
}

// The "Store mapping" menu is built from currently bound slots, but the mapping can be
// cleared or the target module removed in the window between opening the menu and
// clicking the item -- so save() must tolerate a key that no longer matches any slot,
// rather than dereferencing the never-assigned `module` pointer.
TEST_CASE("MemStore::save does not crash and does not store when no slot matches the key", "[MidiCatMem][MidiCat]") {
	Test::Harness h;
	MidiCatModule* midicat = h.addModule<MidiCatModule>("MidiCat");
	MidiCatMemModule* mem = h.addModule<MidiCatMemModule>("MidiCatEx");
	MidiCatMemModule* target = h.addModule<MidiCatMemModule>("MidiCatEx");

	h.connectExpander(midicat, mem);
	h.dspStep();

	// No slot is bound to `target` at all -- every slot has moduleId < 0.
	REQUIRE_NOTHROW(midicat->expanders.memStore().save(
		MemStore::Key(target->model->plugin->slug, target->model->slug), midicat->slots, midicat->paramHandles, MAX_CHANNELS));

	// Nothing was stored: there was no matching module to save.
	REQUIRE_FALSE(midicat->expanders.memStore().test(target));
	REQUIRE(mem->midiMap.empty());
}

TEST_CASE("moduleBindMem restores CC and param binding into MidiCat", "[MidiCatMem][MidiCat]") {
	Test::Harness h;
	MidiCatModule* midicat = h.addModule<MidiCatModule>("MidiCat");
	MidiCatMemModule* mem = h.addModule<MidiCatMemModule>("MidiCatEx");
	MidiCatMemModule* target = h.addModule<MidiCatMemModule>("MidiCatEx");

	h.connectExpander(midicat, mem);
	h.dspStep();

	// Pre-populate midiMap with a mapping for target
	insertMemEntry(mem, target, MidiCatMemModule::PARAM_NEXT, 15);

	// Apply: clears current maps and restores saved ones
	midicat->moduleBindMem(target);

	REQUIRE(midicat->slots[0].cc.getCc() == 15);
	REQUIRE(midicat->paramHandles[0].paramId == MidiCatMemModule::PARAM_NEXT);
	REQUIRE(midicat->paramHandles[0].module == target);
}

TEST_CASE("MemStore::erase removes mapping from storage", "[MidiCatMem][MidiCat]") {
	Test::Harness h;
	MidiCatModule* midicat = h.addModule<MidiCatModule>("MidiCat");
	MidiCatMemModule* mem = h.addModule<MidiCatMemModule>("MidiCatEx");
	MidiCatMemModule* target = h.addModule<MidiCatMemModule>("MidiCatEx");

	h.connectExpander(midicat, mem);
	h.dspStep();
	insertMemEntry(mem, target, 0, 7);

	REQUIRE(midicat->expanders.memStore().test(target));

	midicat->expanders.memStore().erase(MemStore::Key(target->model->plugin->slug, target->model->slug));

	REQUIRE_FALSE(midicat->expanders.memStore().test(target));
	REQUIRE(mem->midiMap.empty());
}

TEST_CASE("moduleRestriction filters MemStore::test by module ID", "[MidiCatMem][MidiCat]") {
	Test::Harness h;
	MidiCatModule* midicat = h.addModule<MidiCatModule>("MidiCat");
	MidiCatMemModule* mem = h.addModule<MidiCatMemModule>("MidiCatEx");
	MidiCatMemModule* targetA = h.addModule<MidiCatMemModule>("MidiCatEx");
	MidiCatMemModule* targetB = h.addModule<MidiCatMemModule>("MidiCatEx");

	h.connectExpander(midicat, mem);
	h.dspStep();

	// Same slug for both targets — one entry in midiMap covers both
	insertMemEntry(mem, targetA, 0, 7);

	// Without restriction both modules match
	REQUIRE(midicat->expanders.memStore().test(targetA));
	REQUIRE(midicat->expanders.memStore().test(targetB));

	// Restrict to targetA's ID only
	mem->moduleRestriction.insert(targetA->getId());

	REQUIRE(midicat->expanders.memStore().test(targetA));       // allowed
	REQUIRE_FALSE(midicat->expanders.memStore().test(targetB)); // blocked by restriction
}