#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"

#include "Reel.cpp"

using namespace StoermelderPackOne::Reel;

SYNC_MODEL(modelReel, "Reel");
Test::TestContext<> testContext;

TEST_CASE("Construction and initialization", "[Reel]") {
	ReelModule* m = Test::createModule<ReelModule>("Reel");
	ReelWidget* mw = Test::createWidget<ReelWidget>("Reel");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);

	Test::destroyWidget(mw);
	Test::destroyModule(m);
}

TEST_CASE("Initial state", "[Reel]") {
	auto module = Test::createModule<ReelModule>("Reel");

	SECTION("Default values") {
		REQUIRE(module->currentSlot == -1);
		REQUIRE(module->slots.empty());
		REQUIRE(module->boundModules.empty());
		REQUIRE(module->boxDraw == 2);
		REQUIRE(module->panelTheme == -1);
	}

	Test::destroyModule(module);
}

TEST_CASE("Slot operations", "[Reel]") {
	auto module = Test::createModule<ReelModule>("Reel");

	SECTION("slotDelete with invalid index does nothing") {
		module->slotDelete(-1);
		module->slotDelete(100);
		REQUIRE(module->slots.empty());
	}

	SECTION("slotDelete removes slot and adjusts currentSlot") {
		// Manually create a slot for testing
		module->slots.emplace_back();
		module->slots[0].used = true;
		module->currentSlot = 0;

		module->slotDelete(0);

		REQUIRE(module->slots.empty());
		REQUIRE(module->currentSlot == -1);
	}

	SECTION("slotDelete adjusts currentSlot when deleting earlier slot") {
		module->slots.emplace_back();
		module->slots[0].used = true;
		module->slots.emplace_back();
		module->slots[1].used = true;
		module->currentSlot = 1;

		module->slotDelete(0);

		REQUIRE(module->slots.size() == 1);
		REQUIRE(module->currentSlot == 0);
	}

	SECTION("slotMove moves slot forward and updates current slot") {
		module->slots.emplace_back(); module->slots[0].used = true; module->slots[0].label = "A";
		module->slots.emplace_back(); module->slots[1].used = true; module->slots[1].label = "B";
		module->slots.emplace_back(); module->slots[2].used = true; module->slots[2].label = "C";
		module->currentSlot = 0;

		module->slotMove(0, 3);

		REQUIRE(module->slots.size() == 3);
		REQUIRE(module->slots[0].label == "B");
		REQUIRE(module->slots[1].label == "C");
		REQUIRE(module->slots[2].label == "A");
		REQUIRE(module->currentSlot == 2);
	}

	SECTION("slotMove moves slot backward and updates current slot") {
		module->slots.emplace_back(); module->slots[0].used = true; module->slots[0].label = "A";
		module->slots.emplace_back(); module->slots[1].used = true; module->slots[1].label = "B";
		module->slots.emplace_back(); module->slots[2].used = true; module->slots[2].label = "C";
		module->currentSlot = 2;

		module->slotMove(2, 0);

		REQUIRE(module->slots.size() == 3);
		REQUIRE(module->slots[0].label == "C");
		REQUIRE(module->slots[1].label == "A");
		REQUIRE(module->slots[2].label == "B");
		REQUIRE(module->currentSlot == 0);
	}

	SECTION("slotMove to end supports trailing-row drop target") {
		module->slots.emplace_back(); module->slots[0].used = true; module->slots[0].label = "A";
		module->slots.emplace_back(); module->slots[1].used = true; module->slots[1].label = "B";
		module->slots.emplace_back(); module->slots[2].used = true; module->slots[2].label = "C";
		module->currentSlot = 1;

		module->slotMove(1, 3);

		REQUIRE(module->slots[0].label == "A");
		REQUIRE(module->slots[1].label == "C");
		REQUIRE(module->slots[2].label == "B");
		REQUIRE(module->currentSlot == 2);
	}

	SECTION("slotMove with invalid destination clamps or ignores safely") {
		module->slots.emplace_back(); module->slots[0].used = true; module->slots[0].label = "A";
		module->slots.emplace_back(); module->slots[1].used = true; module->slots[1].label = "B";

		module->slotMove(0, 100);

		REQUIRE(module->slots[0].label == "B");
		REQUIRE(module->slots[1].label == "A");
	}

	Test::destroyModule(module);
}

TEST_CASE("Bound module management", "[Reel]") {
	auto module = Test::createModule<ReelModule>("Reel");

	SECTION("bindModule with null returns empty string") {
		std::string result = module->bindModule(nullptr);
		REQUIRE(result == "");
		REQUIRE(module->boundModules.empty());
	}

	SECTION("unbindModule with null is safe") {
		module->unbindModule(nullptr);
		REQUIRE(module->boundModules.empty());
	}

	SECTION("clearBoundModules with no bound modules is safe") {
		module->clearBoundModules();
		REQUIRE(module->boundModules.empty());
	}

	Test::destroyModule(module);
}

TEST_CASE("ReelSlot clear", "[Reel]") {
	auto module = Test::createModule<ReelModule>("Reel");

	SECTION("clear on empty slot is safe") {
		ReelModule::ReelSlot slot;
		slot.clear();
		REQUIRE(slot.used == false);
		REQUIRE(slot.label.empty());
		REQUIRE(slot.moduleStates.empty());
		REQUIRE(slot.cablesJ == nullptr);
	}

	SECTION("clear resets all fields") {
		ReelModule::ReelSlot slot;
		slot.used = true;
		slot.label = "Test Label";
		// moduleStates and cablesJ would contain json_t* in real usage

		slot.clear();

		REQUIRE(slot.used == false);
		REQUIRE(slot.label.empty());
		REQUIRE(slot.moduleStates.empty());
		REQUIRE(slot.cablesJ == nullptr);
	}

	Test::destroyModule(module);
}

TEST_CASE("ReelSlot move semantics", "[Reel]") {
	auto module = Test::createModule<ReelModule>("Reel");

	SECTION("move constructor transfers ownership") {
		ReelModule::ReelSlot slot1;
		slot1.used = true;
		slot1.label = "Test";
		module->slots.push_back(std::move(slot1));

		REQUIRE(module->slots.size() == 1);
		REQUIRE(module->slots[0].used == true);
		REQUIRE(module->slots[0].label == "Test");
	}

	SECTION("move assignment transfers ownership") {
		ReelModule::ReelSlot slot1;
		slot1.used = true;
		slot1.label = "Slot 1";

		ReelModule::ReelSlot slot2;
		slot2.used = false;
		slot2.label = "Slot 2";

		slot2 = std::move(slot1);

		REQUIRE(slot2.used == true);
		REQUIRE(slot2.label == "Slot 1");
		REQUIRE(slot1.used == false); // source is cleared
	}

	Test::destroyModule(module);
}

TEST_CASE("ReelSlot non-copyable", "[Reel]") {
	auto module = Test::createModule<ReelModule>("Reel");

	SECTION("copy constructor is deleted") {
		ReelModule::ReelSlot slot1;
		slot1.used = true;

		// This should fail to compile due to deleted copy constructor
		// ReelModule::ReelSlot slot2(slot1);
		// For compile-time verification, we just ensure the type exists
		static_assert(!std::is_copy_constructible<ReelModule::ReelSlot>::value,
			"ReelSlot should not be copy-constructible");
	}

	SECTION("copy assignment is deleted") {
		ReelModule::ReelSlot slot1;
		slot1.used = true;

		ReelModule::ReelSlot slot2;
		// This should fail to compile due to deleted copy assignment
		// slot2 = slot1;
		static_assert(!std::is_copy_assignable<ReelModule::ReelSlot>::value,
			"ReelSlot should not be copy-assignable");
	}

	Test::destroyModule(module);
}

TEST_CASE("Slot copy-paste operations", "[Reel]") {
	auto module = Test::createModule<ReelModule>("Reel");

	SECTION("slotCopyPaste with invalid source does nothing") {
		module->slotCopyPaste(-1, 0);
		module->slotCopyPaste(100, 0);
		REQUIRE(module->slots.empty());
	}

	SECTION("slotCopyPaste with invalid destination does nothing") {
		// Create source slot
		module->slots.emplace_back();
		module->slots[0].used = true;
		module->slots[0].label = "Source";

		module->slotCopyPaste(0, -1);
		REQUIRE(module->slots.size() == 1);
	}

	SECTION("slotCopyPaste copies slot content") {
		// Create source slot
		module->slots.emplace_back();
		module->slots[0].used = true;
		module->slots[0].label = "Source Label";

		module->slotCopyPaste(0, 2);

		REQUIRE(module->slots.size() == 3); // grows to accommodate dst
		REQUIRE(module->slots[0].used == true);
		REQUIRE(module->slots[0].label == "Source Label");
		REQUIRE(module->slots[1].used == false); // intermediate empty
		REQUIRE(module->slots[2].used == true);
		REQUIRE(module->slots[2].label == "Source Label");
	}

	SECTION("slotCopyPaste clears destination before copying") {
		module->slots.emplace_back();
		module->slots[0].used = true;
		module->slots[0].label = "Source";

		module->slots.emplace_back();
		module->slots[1].used = true;
		module->slots[1].label = "Destination";

		module->slotCopyPaste(0, 1);

		REQUIRE(module->slots[1].label == "Source");
	}

	Test::destroyModule(module);
}

TEST_CASE("remapSlotData", "[Reel]") {
	auto module = Test::createModule<ReelModule>("Reel");

	SECTION("with empty slots does nothing") {
		std::map<int64_t, int64_t> oldToNewIds;
		oldToNewIds[1] = 100;

		module->remapSlotData(oldToNewIds);
		REQUIRE(module->slots.empty());
	}

	SECTION("with unused slots does nothing") {
		module->slots.emplace_back();
		module->slots[0].used = false;

		std::map<int64_t, int64_t> oldToNewIds;
		oldToNewIds[1] = 100;

		module->remapSlotData(oldToNewIds);
		REQUIRE(module->slots[0].moduleStates.empty());
	}

	Test::destroyModule(module);
}

TEST_CASE("JSON serialization", "[Reel]") {
	auto module = Test::createModule<ReelModule>("Reel");

	SECTION("dataToJson creates valid JSON") {
		module->boxDraw = true;
		module->boxColor = color::BLUE;
		module->panelTheme = 1;
		module->currentSlot = 0;

		json_t* rootJ = module->dataToJson();

		REQUIRE(rootJ != nullptr);
		REQUIRE(json_object_get(rootJ, "panelTheme") != nullptr);
		REQUIRE(json_object_get(rootJ, "boxDraw") != nullptr);
		REQUIRE(json_object_get(rootJ, "boxColor") != nullptr);
		REQUIRE(json_integer_value(json_object_get(rootJ, "panelTheme")) == 1);

		json_decref(rootJ);
	}

	SECTION("dataToJson includes boundModules array") {
		json_t* rootJ = module->dataToJson();

		json_t* boundModulesJ = json_object_get(rootJ, "boundModules");
		REQUIRE(boundModulesJ != nullptr);
		REQUIRE(json_is_array(boundModulesJ));
		REQUIRE(json_array_size(boundModulesJ) == 0);

		json_decref(rootJ);
	}

	SECTION("dataToJson includes slots array") {
		json_t* rootJ = module->dataToJson();

		json_t* slotsJ = json_object_get(rootJ, "slots");
		REQUIRE(slotsJ != nullptr);
		REQUIRE(json_is_array(slotsJ));
		REQUIRE(json_array_size(slotsJ) == 0);

		json_decref(rootJ);
	}

	SECTION("dataFromJson restores state") {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "panelTheme", json_integer(2));
		json_object_set_new(rootJ, "boxDraw", json_integer(2));
		json_object_set_new(rootJ, "boxColor", json_string("#0000FF"));
		json_object_set_new(rootJ, "currentSlot", json_integer(-1));

		json_t* boundModulesJ = json_array();
		json_object_set_new(rootJ, "boundModules", boundModulesJ);

		json_t* slotsJ = json_array();
		json_object_set_new(rootJ, "slots", slotsJ);

		module->dataFromJson(rootJ);

		REQUIRE(module->panelTheme == 2);
		REQUIRE(module->boxDraw == 2);
		REQUIRE(module->currentSlot == -1);
		REQUIRE(module->boundModules.empty());
		REQUIRE(module->slots.empty());

		json_decref(rootJ);
	}

	SECTION("dataFromJson with empty object uses defaults") {
		json_t* rootJ = json_object();

		module->dataFromJson(rootJ);

		REQUIRE(module->panelTheme == 0);
		REQUIRE(module->currentSlot == -1);
		REQUIRE(module->boxDraw == 2);
		REQUIRE(module->boundModules.empty());

		json_decref(rootJ);
	}

	Test::destroyModule(module);
}

TEST_CASE("slotLoad and slotSave boundary conditions", "[Reel]") {
	auto module = Test::createModule<ReelModule>("Reel");

	SECTION("slotLoad with negative index does nothing") {
		// Should not crash
		module->slotLoad(-1);
		// currentSlot should not change
		REQUIRE(module->currentSlot == -1);
	}

	SECTION("slotLoad with out-of-bounds index does nothing") {
		module->slotLoad(100);
		REQUIRE(module->currentSlot == -1);
	}

	SECTION("slotLoad on unused slot does nothing") {
		module->slots.emplace_back();
		module->slots[0].used = false;

		module->slotLoad(0);
		// Should not change currentSlot or crash
		REQUIRE(module->currentSlot == -1);
	}

	SECTION("slotSave with negative index does nothing") {
		// Should not crash or create slots
		module->slotSave(-1);
		REQUIRE(module->slots.empty());
	}

	Test::destroyModule(module);
}

TEST_CASE("slotDelete boundary conditions", "[Reel]") {
	auto module = Test::createModule<ReelModule>("Reel");

	SECTION("slotDelete with negative index does nothing") {
		module->slotDelete(-1);
		REQUIRE(module->slots.empty());
	}

	SECTION("slotDelete with empty slots does nothing") {
		module->slotDelete(0);
		REQUIRE(module->slots.empty());
	}

	SECTION("slotDelete with valid index removes slot") {
		module->slots.emplace_back();
		module->slots[0].used = true;
		module->slots[0].label = "Slot 1";

		module->slotDelete(0);

		REQUIRE(module->slots.empty());
	}

	SECTION("slotDelete updates currentSlot to -1 when deleting current slot") {
		module->slots.emplace_back();
		module->slots[0].used = true;
		module->currentSlot = 0;

		module->slotDelete(0);

		REQUIRE(module->currentSlot == -1);
	}

	SECTION("slotDelete adjusts currentSlot down when deleting earlier slot") {
		module->slots.emplace_back();
		module->slots[0].used = true;
		module->slots.emplace_back();
		module->slots[1].used = true;
		module->slots.emplace_back();
		module->slots[2].used = true;
		module->currentSlot = 2;

		module->slotDelete(0);

		REQUIRE(module->slots.size() == 2);
		REQUIRE(module->currentSlot == 1); // was 2, now decremented
	}

	Test::destroyModule(module);
}
