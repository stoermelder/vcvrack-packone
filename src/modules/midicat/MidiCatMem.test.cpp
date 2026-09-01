#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"
#include "MidiCatMem.cpp"
#include "MidiCat.cpp"
#include "../midi/MidiTrackingProcessor.hpp"

using namespace StoermelderPackOne::MidiCat;

SYNC_MODEL(modelMidiCat, "MidiCat");
SYNC_MODEL(modelMidiCatMem, "MidiCatEx");
Test::TestContext<> testContext;

// Helper: connect MidiCatMem to MidiCat as right expander
static void connectMem(MidiCatModule* midicat, MidiCatMemModule* mem) {
	midicat->rightExpander.module = mem;
	mem->leftExpander.module = midicat;
	midicat->moduleChangedFlag = true;
	midicat->process(Test::makeProcessArgs(1));
}

// Helper: set up a CC+param binding on a given MidiCat channel.
// The target module must already be registered in the engine.
static void setupBinding(MidiCatModule* midicat, Module* target, int channel, int cc, int paramId) {
	midicat->processDivider.setDivision(1);
	midicat->enableLearn(channel, true);
	midicat->midiInput.onMessage(Test::makeMidiMessage(0xb, 0, cc, 64));
	midicat->learnParam(channel, target->id, paramId);
	midicat->process(Test::makeProcessArgs(1));
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
	MidiCatMemModule* m = Test::createModule<MidiCatMemModule>("MidiCatEx");

	REQUIRE(m != nullptr);
	REQUIRE(m->NUM_PARAMS == 3);
	REQUIRE(m->NUM_INPUTS == 2);
	REQUIRE(m->NUM_OUTPUTS == 0);
	REQUIRE(m->NUM_LIGHTS == 1);
	REQUIRE(m->midiMap.empty());
	REQUIRE(m->moduleRestriction.empty());

	Test::destroyModule(m);
}

TEST_CASE("Preset JSON null-guards", "[MidiCatMem][JSON]") {
	auto module = Test::createModule<MidiCatMemModule>("MidiCatEx");

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

	Test::destroyModule(module);
}

TEST_CASE("JSON round-trip preserves state", "[MidiCatMem]") {
	MidiCatMemModule* m = Test::createModule<MidiCatMemModule>("MidiCatEx");

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

	Test::destroyModule(m);
}


// ─── Standalone tests ───────────────────────────────────────────────────────

TEST_CASE("process() publishes midiMap via leftExpander", "[MidiCatMem]") {
	MidiCatMemModule* m = Test::createModule<MidiCatMemModule>("MidiCatEx");

	m->process(Test::makeProcessArgs(1));

	REQUIRE(m->leftExpander.producerMessage == &m->midiMap);
	REQUIRE(m->leftExpander.messageFlipRequested == true);

	Test::destroyModule(m);
}

TEST_CASE("process() does not crash without left expander", "[MidiCatMem]") {
	MidiCatMemModule* m = Test::createModule<MidiCatMemModule>("MidiCatEx");
	REQUIRE_NOTHROW(m->process(Test::makeProcessArgs(1)));
	Test::destroyModule(m);
}

TEST_CASE("onReset clears midiMap and moduleRestriction", "[MidiCatMem]") {
	MidiCatMemModule* m = Test::createModule<MidiCatMemModule>("MidiCatEx");

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

	Test::destroyModule(m);
}

// ─── Integration tests ──────────────────────────────────────────────────────

TEST_CASE("MidiCat detects expander", "[MidiCatMem][MidiCat]") {
	MidiCatModule* midicat = Test::createModule<MidiCatModule>("MidiCat");
	MidiCatMemModule* mem   = Test::createModule<MidiCatMemModule>("MidiCatEx");
	// MidiCat.expanders.hpp detects mem via `exp->model == modelMidiCatMem` — a mismatch here
	// (missing/wrong SYNC_MODEL) would make the REQUIRE below fail with no useful diagnosis.
	Test::requireModelSync(modelMidiCatMem, "MidiCatEx");
	Test::registerModule(midicat);
	Test::registerModule(mem);

	// Flush initial expandersChanged so expMem is properly null before connecting
	midicat->process(Test::makeProcessArgs(0));
	REQUIRE(midicat->expanders.mem() == nullptr);

	connectMem(midicat, mem);

	REQUIRE(midicat->expanders.mem() != nullptr);
	REQUIRE(midicat->expanders.mem() == dynamic_cast<MidiCatMemBase*>(mem));

	Test::unregisterModule(mem);
	Test::destroyModule(mem);
	Test::unregisterModule(midicat);
	Test::destroyModule(midicat);
}

TEST_CASE("Disconnecting expander clears expMem", "[MidiCatMem][MidiCat]") {
	MidiCatModule* midicat = Test::createModule<MidiCatModule>("MidiCat");
	MidiCatMemModule* mem   = Test::createModule<MidiCatMemModule>("MidiCatEx");
	Test::registerModule(midicat);
	Test::registerModule(mem);

	connectMem(midicat, mem);
	REQUIRE(midicat->expanders.mem() != nullptr);

	midicat->rightExpander.module = nullptr;
	mem->leftExpander.module = nullptr;
	midicat->moduleChangedFlag = true;
	midicat->process(Test::makeProcessArgs(10));

	REQUIRE(midicat->expanders.mem() == nullptr);

	Test::unregisterModule(mem);
	Test::destroyModule(mem);
	Test::unregisterModule(midicat);
	Test::destroyModule(midicat);
}

TEST_CASE("MemStore::test returns false for unknown module", "[MidiCatMem][MidiCat]") {
	MidiCatModule* midicat    = Test::createModule<MidiCatModule>("MidiCat");
	MidiCatMemModule* mem     = Test::createModule<MidiCatMemModule>("MidiCatEx");
	MidiCatMemModule* unknown = Test::createModule<MidiCatMemModule>("MidiCatEx");
	Test::registerModule(midicat);
	Test::registerModule(mem);
	Test::registerModule(unknown);

	connectMem(midicat, mem);
	// midiMap is empty, so no slug matches
	REQUIRE_FALSE(midicat->expanders.memStore().test(unknown));

	Test::unregisterModule(unknown);
	Test::destroyModule(unknown);
	Test::unregisterModule(mem);
	Test::destroyModule(mem);
	Test::unregisterModule(midicat);
	Test::destroyModule(midicat);
}

TEST_CASE("MemStore::save stores current MidiCat CC mapping", "[MidiCatMem][MidiCat]") {
	MidiCatModule* midicat = Test::createModule<MidiCatModule>("MidiCat");
	MidiCatMemModule* mem  = Test::createModule<MidiCatMemModule>("MidiCatEx");
	// Use a second MidiCatMemModule as target (it has parameters and a proper model)
	MidiCatMemModule* target = Test::createModule<MidiCatMemModule>("MidiCatEx");
	Test::registerModule(midicat);
	Test::registerModule(mem);
	Test::registerModule(target);

	// Bind CC 7 → target PARAM_APPLY (id=0)
	setupBinding(midicat, target, 0, 7, MidiCatMemModule::PARAM_APPLY);

	connectMem(midicat, mem);

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

	Test::unregisterModule(target);
	Test::destroyModule(target);
	Test::unregisterModule(mem);
	Test::destroyModule(mem);
	Test::unregisterModule(midicat);
	Test::destroyModule(midicat);
}

// The "Store mapping" menu is built from currently bound slots, but the mapping can be
// cleared or the target module removed in the window between opening the menu and
// clicking the item -- so save() must tolerate a key that no longer matches any slot,
// rather than dereferencing the never-assigned `module` pointer.
TEST_CASE("MemStore::save does not crash and does not store when no slot matches the key", "[MidiCatMem][MidiCat]") {
	MidiCatModule* midicat = Test::createModule<MidiCatModule>("MidiCat");
	MidiCatMemModule* mem  = Test::createModule<MidiCatMemModule>("MidiCatEx");
	MidiCatMemModule* target = Test::createModule<MidiCatMemModule>("MidiCatEx");
	Test::registerModule(midicat);
	Test::registerModule(mem);
	Test::registerModule(target);

	connectMem(midicat, mem);

	// No slot is bound to `target` at all -- every slot has moduleId < 0.
	REQUIRE_NOTHROW(midicat->expanders.memStore().save(
		MemStore::Key(target->model->plugin->slug, target->model->slug), midicat->slots, midicat->paramHandles, MAX_CHANNELS));

	// Nothing was stored: there was no matching module to save.
	REQUIRE_FALSE(midicat->expanders.memStore().test(target));
	REQUIRE(mem->midiMap.empty());

	Test::unregisterModule(target);
	Test::destroyModule(target);
	Test::unregisterModule(mem);
	Test::destroyModule(mem);
	Test::unregisterModule(midicat);
	Test::destroyModule(midicat);
}

TEST_CASE("moduleBindMem restores CC and param binding into MidiCat", "[MidiCatMem][MidiCat]") {
	MidiCatModule* midicat = Test::createModule<MidiCatModule>("MidiCat");
	MidiCatMemModule* mem  = Test::createModule<MidiCatMemModule>("MidiCatEx");
	MidiCatMemModule* target = Test::createModule<MidiCatMemModule>("MidiCatEx");
	Test::registerModule(midicat);
	Test::registerModule(mem);
	Test::registerModule(target);

	connectMem(midicat, mem);

	// Pre-populate midiMap with a mapping for target
	insertMemEntry(mem, target, MidiCatMemModule::PARAM_NEXT, 15);

	// Apply: clears current maps and restores saved ones
	midicat->moduleBindMem(target);

	REQUIRE(midicat->slots[0].cc.getCc() == 15);
	REQUIRE(midicat->paramHandles[0].paramId == MidiCatMemModule::PARAM_NEXT);
	REQUIRE(midicat->paramHandles[0].module == target);

	Test::unregisterModule(target);
	Test::destroyModule(target);
	Test::unregisterModule(mem);
	Test::destroyModule(mem);
	Test::unregisterModule(midicat);
	Test::destroyModule(midicat);
}

TEST_CASE("MemStore::erase removes mapping from storage", "[MidiCatMem][MidiCat]") {
	MidiCatModule* midicat = Test::createModule<MidiCatModule>("MidiCat");
	MidiCatMemModule* mem  = Test::createModule<MidiCatMemModule>("MidiCatEx");
	MidiCatMemModule* target = Test::createModule<MidiCatMemModule>("MidiCatEx");
	Test::registerModule(midicat);
	Test::registerModule(mem);
	Test::registerModule(target);

	connectMem(midicat, mem);
	insertMemEntry(mem, target, 0, 7);

	REQUIRE(midicat->expanders.memStore().test(target));

	midicat->expanders.memStore().erase(MemStore::Key(target->model->plugin->slug, target->model->slug));

	REQUIRE_FALSE(midicat->expanders.memStore().test(target));
	REQUIRE(mem->midiMap.empty());

	Test::unregisterModule(target);
	Test::destroyModule(target);
	Test::unregisterModule(mem);
	Test::destroyModule(mem);
	Test::unregisterModule(midicat);
	Test::destroyModule(midicat);
}

TEST_CASE("moduleRestriction filters MemStore::test by module ID", "[MidiCatMem][MidiCat]") {
	MidiCatModule* midicat  = Test::createModule<MidiCatModule>("MidiCat");
	MidiCatMemModule* mem   = Test::createModule<MidiCatMemModule>("MidiCatEx");
	MidiCatMemModule* targetA = Test::createModule<MidiCatMemModule>("MidiCatEx");
	MidiCatMemModule* targetB = Test::createModule<MidiCatMemModule>("MidiCatEx");
	Test::registerModule(midicat);
	Test::registerModule(mem);
	Test::registerModule(targetA);
	Test::registerModule(targetB);

	connectMem(midicat, mem);

	// Same slug for both targets — one entry in midiMap covers both
	insertMemEntry(mem, targetA, 0, 7);

	// Without restriction both modules match
	REQUIRE(midicat->expanders.memStore().test(targetA));
	REQUIRE(midicat->expanders.memStore().test(targetB));

	// Restrict to targetA's ID only
	mem->moduleRestriction.insert(targetA->getId());

	REQUIRE(midicat->expanders.memStore().test(targetA));       // allowed
	REQUIRE_FALSE(midicat->expanders.memStore().test(targetB)); // blocked by restriction

	Test::unregisterModule(targetB);
	Test::destroyModule(targetB);
	Test::unregisterModule(targetA);
	Test::destroyModule(targetA);
	Test::unregisterModule(mem);
	Test::destroyModule(mem);
	Test::unregisterModule(midicat);
	Test::destroyModule(midicat);
}