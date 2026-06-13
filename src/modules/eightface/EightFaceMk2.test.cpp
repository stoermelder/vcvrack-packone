#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"
#include "EightFaceMk2.cpp"
#include "EightFaceMk2Ex.cpp"

using namespace StoermelderPackOne::EightFaceMk2;

SYNC_MODEL(modelEightFaceMk2, "EightFaceMk2");
SYNC_MODEL(modelEightFaceMk2Ex, "EightFaceMk2Ex");
Test::TestContext<> testContext;

TEST_CASE("Construction and initialization", "[EightFaceMk2]") {
	EightFaceMk2Module<8>* m = Test::createModule<EightFaceMk2Module<8>>("EightFaceMk2");
	EightFaceMk2Widget<8>* mw = Test::createWidget<EightFaceMk2Widget<8>>("EightFaceMk2");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);

	Test::destroyWidget(mw);
	Test::destroyModule(m);
}

TEST_CASE("Preset JSON null-guards", "[EightFaceMk2][JSON]") {
	auto module = Test::createModule<EightFaceMk2Module<8>>("EightFaceMk2");

	SECTION("All top-level properties are null-guarded in dataFromJson()") {
		json_t* rootJ = module->dataToJson();
		REQUIRE(rootJ != nullptr);
		Test::testPresetNullGuards(module, rootJ);
		json_decref(rootJ);
	}

	Test::destroyModule(module);
}

TEST_CASE("EightFaceMk2Ex Construction and initialization", "[EightFaceMk2]") {
	EightFaceMk2ExModule<8>* m = Test::createModule<EightFaceMk2ExModule<8>>("EightFaceMk2Ex");
	EightFaceMk2ExWidget<8>* mw = Test::createWidget<EightFaceMk2ExWidget<8>>("EightFaceMk2Ex");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);

	Test::destroyWidget(mw);
	Test::destroyModule(m);
}

TEST_CASE("processGui does not decrement refcount of slot-owned json objects", "[EightFaceMk2]") {
	// Regression test: commit 84866bc incorrectly added json_decref(vJ) inside processGui().
	// vJ pointers in workerGuiQueue are owned by slot->preset — processGui must not
	// touch the refcount or the preset slot's json_t* becomes a dangling pointer.

	EightFaceMk2Module<8>* m = Test::createModule<EightFaceMk2Module<8>>("EightFaceMk2");
	// A second module instance acts as the "bound" module whose preset is being loaded.
	EightFaceMk2Module<8>* boundM = Test::createModule<EightFaceMk2Module<8>>("EightFaceMk2");
	EightFaceMk2Widget<8>* boundMw = Test::createWidget<EightFaceMk2Widget<8>>(boundM);

	// Use GUI mode: processGui calls boundMw->module->fromJson(vJ).
	m->guiSafeMode = GUISAFEMODE::GUI;

	// Build a valid preset JSON matching the bound module's plugin/model slugs.
	json_t* vJ = m->toJson();
	size_t refcount = vJ->refcount;

	m->workerGuiQueue.push(std::make_tuple(boundMw, vJ));
	m->processGui();

	REQUIRE(m->workerGuiQueue.empty());
	REQUIRE(json_typeof(vJ) == JSON_OBJECT);
	REQUIRE(vJ->refcount == refcount);

	json_decref(vJ);

	Test::destroyWidget(boundMw);
	Test::destroyModule(boundM);
	Test::destroyModule(m);
}