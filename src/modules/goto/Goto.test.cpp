#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"

#include "Goto.cpp"

using namespace StoermelderPackOne::Goto;

SYNC_MODEL(modelGoto, "Goto");
Test::TestContext<> testContext;

TEST_CASE("Construction and initialization", "[Goto]") {
	GotoModule<10>* module = Test::createModule<GotoModule<10>>("Goto");
	GotoWidget* mw = Test::createWidget<GotoWidget>(module);

	Test::destroyWidget(mw);
	Test::destroyModule(module);
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

TEST_CASE("JSON round-trip preserves all settings", "[Goto]") {
	auto module = Test::createModule<GotoModule<10>>("Goto");

	module->triggerMode = TRIGGERMODE::C5;
	module->smoothTransition = true;
	module->jumpPos = JUMPPOS::MODULE_TOPLEFT;
	module->ignoreZoom = true;

	module->jumpPoints[0].moduleIds = {111, 222};
	module->jumpPoints[0].zoom = 0.5f;
	module->jumpPoints[5].moduleIds = {999};
	module->jumpPoints[5].zoom = 1.0f;

	json_t* j = module->dataToJson();

	auto module2 = Test::createModule<GotoModule<10>>("Goto");
	module2->dataFromJson(j);
	json_decref(j);

	SECTION("triggerMode preserved") {
		REQUIRE(module2->triggerMode == TRIGGERMODE::C5);
	}

	SECTION("smoothTransition preserved") {
		REQUIRE(module2->smoothTransition == true);
	}

	SECTION("jumpPos preserved") {
		REQUIRE(module2->jumpPos == JUMPPOS::MODULE_TOPLEFT);
	}

	SECTION("ignoreZoom preserved") {
		REQUIRE(module2->ignoreZoom == true);
	}

	SECTION("Jump point 0 module IDs preserved") {
		REQUIRE(module2->jumpPoints[0].moduleIds.size() == 2);
		REQUIRE(module2->jumpPoints[0].moduleIds[0] == 111);
		REQUIRE(module2->jumpPoints[0].moduleIds[1] == 222);
	}

	SECTION("Jump point 0 zoom preserved") {
		REQUIRE(module2->jumpPoints[0].zoom == Approx(0.5f));
	}

	SECTION("Jump point 5 module IDs preserved") {
		REQUIRE(module2->jumpPoints[5].moduleIds.size() == 1);
		REQUIRE(module2->jumpPoints[5].moduleIds[0] == 999);
	}

	SECTION("Empty jump points remain empty") {
		REQUIRE(module2->jumpPoints[1].moduleIds.empty());
		REQUIRE(module2->jumpPoints[9].moduleIds.empty());
	}

	Test::destroyModule(module);
	Test::destroyModule(module2);
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