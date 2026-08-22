#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"
#include "../../test/test_mock.hpp"

#include "Goto.cpp"

using namespace StoermelderPackOne;
using namespace StoermelderPackOne::Goto;

SYNC_MODEL(modelGoto, "Goto");
Test::TestContext<> testContext;

// A ModuleAccess mock that records getModuleWidget lookups (returns nullptr).
struct MockModuleAccess : vcv::ModuleAccess {
	mutable std::vector<int64_t> getModuleWidgetCalls;
	ModuleWidget* getModuleWidget(int64_t moduleId) const override {
		getModuleWidgetCalls.push_back(moduleId);
		return nullptr;
	}
};


TEST_CASE("Construction and initialization", "[Goto]") {
	GotoModule<10>* m = Test::createModule<GotoModule<10>>("Goto");
	GotoWidget* mw = Test::createWidget<GotoWidget>("Goto");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);

	Test::destroyWidget(mw);
	Test::destroyModule(m);
}

TEST_CASE("Preset JSON null-guards", "[Goto][JSON]") {
	auto module = Test::createModule<GotoModule<10>>("Goto");

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

TEST_CASE("JSON round-trip preserves state", "[Goto]") {
	auto module = Test::createModule<GotoModule<10>>("Goto");
	auto module2 = Test::createModule<GotoModule<10>>("Goto");

	SECTION("Scalar settings round-trip") {
		// Distinct, non-default values for every scalar stored to JSON
		module->panelTheme = 1;
		module->triggerMode = TRIGGERMODE::C5;
		module->smoothTransition = true;
		module->jumpPos = JUMPPOS::MODULE_TOPLEFT;
		module->ignoreZoom = true;

		json_t* j = module->dataToJson();
		module2->dataFromJson(j);
		json_decref(j);

		REQUIRE(module2->panelTheme == 1);
		REQUIRE(module2->triggerMode == TRIGGERMODE::C5);
		REQUIRE(module2->smoothTransition == true);
		REQUIRE(module2->jumpPos == JUMPPOS::MODULE_TOPLEFT);
		REQUIRE(module2->ignoreZoom == true);
	}

	SECTION("jumpPoints array round-trips (variable moduleIds count + zoom)") {
		// Use a non-uniform shape: vary the number of moduleIds per slot
		// (0, 1, 2, 3, ...) and non-linear zoom values, so an indexing or
		// scaling bug would be caught.
		for (int i = 0; i < 10; i++) {
			module->jumpPoints[i].moduleIds.clear();
			size_t count = i % 4;
			for (size_t k = 0; k < count; k++) {
				module->jumpPoints[i].moduleIds.push_back(i * 1000 + (int)k);
			}
			module->jumpPoints[i].zoom = 0.25f + 0.1f * i;
		}

		json_t* j = module->dataToJson();
		module2->dataFromJson(j);
		json_decref(j);

		for (int i = 0; i < 10; i++) {
			size_t count = i % 4;
			REQUIRE(module2->jumpPoints[i].moduleIds.size() == count);
			for (size_t k = 0; k < count; k++) {
				REQUIRE(module2->jumpPoints[i].moduleIds[k] == i * 1000 + (int)k);
			}
			REQUIRE(module2->jumpPoints[i].zoom == Catch::Approx(0.25f + 0.1f * i).margin(0.01f));
		}
	}

	Test::destroyModule(module);
	Test::destroyModule(module2);
}


TEST_CASE("POLYTRIGGER mode sets jumpTrigger on rising edge", "[Goto]") {
	auto module = Test::createModule<GotoModule<10>>("Goto");
	module->triggerMode = TRIGGERMODE::POLYTRIGGER;

	// Simulate connected cable
	module->inputs[GotoModule<10>::INPUT_TRIG].channels = 10;

	// Low voltage first — no trigger
	for (int i = 0; i < 10; i++) {
		module->inputs[GotoModule<10>::INPUT_TRIG].setVoltage(0.f, i);
	}
	module->process(Test::makeProcessArgs(1));
	REQUIRE(module->jumpTrigger == -1);

	// Rising edge on channel 4 (slot 4)
	module->inputs[GotoModule<10>::INPUT_TRIG].setVoltage(10.f, 4);
	module->process(Test::makeProcessArgs(2));

	SECTION("jumpTrigger is set to slot 4") {
		REQUIRE(module->jumpTrigger == 4);
	}

	Test::destroyModule(module);
}

TEST_CASE("POLYTRIGGER: no trigger when voltage stays high (no new edge)", "[Goto]") {
	auto module = Test::createModule<GotoModule<10>>("Goto");
	module->triggerMode = TRIGGERMODE::POLYTRIGGER;
	module->inputs[GotoModule<10>::INPUT_TRIG].channels = 10;

	// Prime triggers with a high-then-low cycle
	for (int i = 0; i < 10; i++) {
		module->inputs[GotoModule<10>::INPUT_TRIG].setVoltage(0.f, i);
	}
	module->process(Test::makeProcessArgs(1));
	module->inputs[GotoModule<10>::INPUT_TRIG].setVoltage(10.f, 2);
	module->process(Test::makeProcessArgs(2));
	module->jumpTrigger = -1; // simulate widget consuming the trigger

	// Process again with voltage still high — Schmitt trigger should NOT re-fire
	module->process(Test::makeProcessArgs(3));

	SECTION("jumpTrigger remains -1 (no retriggering on sustained high)") {
		REQUIRE(module->jumpTrigger == -1);
	}

	Test::destroyModule(module);
}

TEST_CASE("C5 trigger mode maps voltage to slot", "[Goto]") {
	auto module = Test::createModule<GotoModule<10>>("Goto");
	module->triggerMode = TRIGGERMODE::C5;
	module->inputs[GotoModule<10>::INPUT_TRIG].channels = 1;

	// In C5 mode: slot t = (voltage - 1) * 12; so voltage=1.0 → t=0 (slot 0)
	module->triggerVoltage = 0.f; // reset last seen voltage

	module->inputs[GotoModule<10>::INPUT_TRIG].setVoltage(1.0f); // slot 0
	module->process(Test::makeProcessArgs(1));

	SECTION("Slot 0 triggered at 1.0 V") {
		REQUIRE(module->jumpTrigger == 0);
	}

	Test::destroyModule(module);
}

TEST_CASE("C5 trigger mode: slot 3 at correct voltage", "[Goto]") {
	auto module = Test::createModule<GotoModule<10>>("Goto");
	module->triggerMode = TRIGGERMODE::C5;
	module->inputs[GotoModule<10>::INPUT_TRIG].channels = 1;

	// slot 3: voltage = 1 + 3/12 = 1.25
	module->triggerVoltage = 0.f;
	module->inputs[GotoModule<10>::INPUT_TRIG].setVoltage(1.25f);
	module->process(Test::makeProcessArgs(1));

	SECTION("Slot 3 triggered at 1.25 V") {
		REQUIRE(module->jumpTrigger == 3);
	}

	Test::destroyModule(module);
}

TEST_CASE("C5 trigger mode: out-of-range voltage is ignored", "[Goto]") {
	auto module = Test::createModule<GotoModule<10>>("Goto");
	module->triggerMode = TRIGGERMODE::C5;
	module->inputs[GotoModule<10>::INPUT_TRIG].channels = 1;

	// slot = (2.5 - 1) * 12 = 18 — beyond SLOTS-1=9
	module->triggerVoltage = 0.f;
	module->inputs[GotoModule<10>::INPUT_TRIG].setVoltage(2.5f);
	module->process(Test::makeProcessArgs(1));

	SECTION("No jump triggered for out-of-range voltage") {
		REQUIRE(module->jumpTrigger == -1);
	}

	Test::destroyModule(module);
}

TEST_CASE("C5 trigger: same voltage twice does not re-trigger", "[Goto]") {
	auto module = Test::createModule<GotoModule<10>>("Goto");
	module->triggerMode = TRIGGERMODE::C5;
	module->inputs[GotoModule<10>::INPUT_TRIG].channels = 1;

	module->triggerVoltage = 0.f;
	module->inputs[GotoModule<10>::INPUT_TRIG].setVoltage(1.0f);
	module->process(Test::makeProcessArgs(1));

	// Simulate widget consuming the trigger
	module->jumpTrigger = -1;

	// Same voltage again — should NOT trigger because triggerVoltage == current v
	module->process(Test::makeProcessArgs(2));

	SECTION("jumpTrigger stays -1 on repeated same voltage") {
		REQUIRE(module->jumpTrigger == -1);
	}

	Test::destroyModule(module);
}

TEST_CASE("jumpTriggerUsed reflects cable connection state", "[Goto]") {
	auto module = Test::createModule<GotoModule<10>>("Goto");

	SECTION("False when input disconnected") {
		module->inputs[GotoModule<10>::INPUT_TRIG].channels = 0;
		module->process(Test::makeProcessArgs(1));
		REQUIRE(module->jumpTriggerUsed == false);
	}

	SECTION("True when input connected") {
		module->inputs[GotoModule<10>::INPUT_TRIG].channels = 1;
		module->process(Test::makeProcessArgs(1));
		REQUIRE(module->jumpTriggerUsed == true);
	}

	Test::destroyModule(module);
}


TEST_CASE("JSON legacy single-moduleId field is loaded correctly", "[Goto]") {
	auto module = Test::createModule<GotoModule<10>>("Goto");

	// Build a JSON payload that uses the old "moduleId" key instead of "moduleIds"
	json_t* rootJ = json_object();
	json_object_set_new(rootJ, "panelTheme", json_integer(0));
	json_object_set_new(rootJ, "triggerMode", json_integer(0));
	json_object_set_new(rootJ, "smoothTransition", json_false());
	json_object_set_new(rootJ, "centerModule", json_integer((int)JUMPPOS::MODULE_CENTER));
	json_object_set_new(rootJ, "ignoreZoom", json_false());

	json_t* jumpPointsJ = json_array();
	for (int i = 0; i < 10; i++) {
		json_t* jpJ = json_object();
		if (i == 2) {
			// legacy single-ID format
			json_object_set_new(jpJ, "moduleId", json_integer(777));
		} else {
			json_object_set_new(jpJ, "moduleIds", json_array());
		}
		json_object_set_new(jpJ, "zoom", json_real(1.f));
		json_array_append_new(jumpPointsJ, jpJ);
	}
	json_object_set_new(rootJ, "jumpPoints", jumpPointsJ);

	module->dataFromJson(rootJ);
	json_decref(rootJ);

	SECTION("Legacy moduleId migrated to moduleIds vector") {
		REQUIRE(module->jumpPoints[2].moduleIds.size() == 1);
		REQUIRE(module->jumpPoints[2].moduleIds[0] == 777);
	}

	SECTION("Other jump points remain empty") {
		REQUIRE(module->jumpPoints[0].moduleIds.empty());
		REQUIRE(module->jumpPoints[9].moduleIds.empty());
	}

	Test::destroyModule(module);
}

TEST_CASE("executeJump routes through the module access layer", "[Goto][vcv]") {
	auto mock = Test::makeMockVcv<MockModuleAccess>();
	auto module = Test::createModule<GotoModule<10>>("Goto");

	GotoContainer<10> container;
	container.module = module;
	container.mw = nullptr;

	module->jumpPoints[0].moduleIds = {42, 43};
	container.executeJump(0);

	REQUIRE(mock.modules.getModuleWidgetCalls.size() == 2);
	CHECK(mock.modules.getModuleWidgetCalls[0] == 42);
	CHECK(mock.modules.getModuleWidgetCalls[1] == 43);

	Test::destroyModule(module);
}