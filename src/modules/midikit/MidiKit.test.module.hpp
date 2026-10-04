TEST_CASE("Construction and initialization", "[MidiKit]") {
	Kit<> kit;
	MidiKitModule* m = kit.m;

	REQUIRE(m != nullptr);
	REQUIRE(m->NUM_PARAMS == 4);
	REQUIRE(m->NUM_INPUTS == 6);   // 4 voltage + 2 trigger
	REQUIRE(m->NUM_OUTPUTS == 2);  // 2 trigger out
	REQUIRE(m->NUM_LIGHTS == 0);
	REQUIRE(m->host.script == "");
	REQUIRE(m->triggerIns.triggerTick[0][0] == 0);
}


TEST_CASE("Preset JSON null-guards", "[MidiKit][JSON]") {
	Kit<> kit;
	MidiKitModule* module = kit.m;

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


TEST_CASE("process() does not crash with no script", "[MidiKit]") {
	Kit<> kit;
	MidiKitModule* m = kit.m;

	// With no engine loaded, the dispatch path (processInMessage/processInTick/
	// activeEngine->process()) is skipped, but the module's own out-queue drain
	// is unconditional — it must simply not crash.
	for (int i = 0; i < 20; i++) {
		REQUIRE_NOTHROW(m->process(Test::makeProcessArgs(i + 1)));
	}

	REQUIRE(m->timingCurrentFrame.load() == 20);
}

TEST_CASE("Default engine is not set", "[MidiKit]") {
	Kit<> kit;
	MidiKitModule* m = kit.m;

	REQUIRE(m->host.getActiveEngine() == nullptr);
}

TEST_CASE("@engine minilua@v1 header selects Lua engine", "[MidiKit]") {
	Kit<> kit;
	MidiKitModule* m = kit.m;

	m->loadScript(LUA_SCRIPT);

	REQUIRE(m->host.isLuaEngine());
}

TEST_CASE("QuickJs header keeps QuickJs engine active", "[MidiKit]") {
	Kit<> kit;
	MidiKitModule* m = kit.m;

	// First switch to Lua, then switch back via a QuickJs-tagged script
	m->loadScript(LUA_SCRIPT);
	REQUIRE(m->host.isLuaEngine());

	m->loadScript(QUICKJS_SCRIPT);
	REQUIRE(m->host.isQuickJsEngine());
}

TEST_CASE("clearScript resets to empty and restores no engine", "[MidiKit]") {
	Kit<> kit;
	MidiKitModule* m = kit.m;

	m->loadScript(LUA_SCRIPT);
	REQUIRE(m->host.isLuaEngine());

	m->clearScript();

	REQUIRE(m->host.script == "");
	REQUIRE(m->host.getActiveEngine() == nullptr);
}

TEST_CASE("Trigger input increments triggerTick", "[MidiKit]") {
	Kit<> kit;
	MidiKitModule* m = kit.m;

	// With no default engine, load a script so process() runs past the
	// `if (!activeEngine) return;` guard. The trigger is enabled directly here
	// (as the script's trig.enableIn(1) would do) so the module processes ticks.
	m->loadScript(QUICKJS_SCRIPT);
	m->enableTrigger(0, 0);

	m->inputs[MidiKitModule::INPUT_TRIG].channels = 1;

	// Prime the SchmittTrigger to LOW state before the first rising edge
	m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(0.f);
	m->process(Test::makeProcessArgs(0));

	// Rising edge → tick increments
	m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(10.f);
	m->process(Test::makeProcessArgs(1));
	REQUIRE(m->triggerIns.triggerTick[0][0] == 1);

	m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(0.f);
	m->process(Test::makeProcessArgs(2));
	REQUIRE(m->triggerIns.triggerTick[0][0] == 1);  // no change on falling edge

	// Second pulse
	m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(10.f);
	m->process(Test::makeProcessArgs(3));
	REQUIRE(m->triggerIns.triggerTick[0][0] == 2);
}

TEST_CASE("Trigger input is not processed until the trigger is enabled", "[MidiKit]") {
	Kit<> kit;
	MidiKitModule* m = kit.m;

	// With no default engine, load a script so process() runs past the
	// `if (!activeEngine) return;` guard.
	m->loadScript(QUICKJS_SCRIPT);

	m->inputs[MidiKitModule::INPUT_TRIG].channels = 1;

	// Prime the SchmittTrigger LOW, then pulse — without trig.enableIn() the
	// module must not process triggers at all: no tick counting, no
	// tick-scheduled drains, and no trig.onTrigger dispatch.
	m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(0.f);
	m->process(Test::makeProcessArgs(0));
	m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(10.f);
	m->process(Test::makeProcessArgs(1));
	m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(0.f);
	m->process(Test::makeProcessArgs(2));
	m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(10.f);
	m->process(Test::makeProcessArgs(3));
	REQUIRE(m->triggerIns.triggerTick[0][0] == 0);

	// Enabling the channel (as the script's trig.enableIn(1) would do) turns
	// trigger processing on — the next rising edge counts a tick.
	m->enableTrigger(0, 0);
	m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(0.f);
	m->process(Test::makeProcessArgs(4));
	m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(10.f);
	m->process(Test::makeProcessArgs(5));
	REQUIRE(m->triggerIns.triggerTick[0][0] == 1);
}

TEST_CASE("Polyphonic trigger input counts ticks per channel", "[MidiKit]") {
	Kit<> kit;
	MidiKitModule* m = kit.m;

	m->loadScript(QUICKJS_SCRIPT);

	// Enable both trigger channels (as the script's trig.enableIn(1, 1) and
	// trig.enableIn(1, 2) would do) so the module counts ticks on each.
	m->enableTrigger(0, 0);
	m->enableTrigger(0, 1);

	m->inputs[MidiKitModule::INPUT_TRIG].channels = 2;

	// Prime both SchmittTriggers LOW before the first rising edges.
	m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(0.f, 0);
	m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(0.f, 1);
	m->process(Test::makeProcessArgs(0));

	// Channel 1 fires twice, channel 2 fires once.
	m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(10.f, 0);
	m->process(Test::makeProcessArgs(1));
	m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(0.f, 0);
	m->process(Test::makeProcessArgs(2));
	m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(10.f, 0);
	m->process(Test::makeProcessArgs(3));
	m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(10.f, 1);
	m->process(Test::makeProcessArgs(4));

	REQUIRE(m->triggerIns.triggerTick[0][0] == 2);
	REQUIRE(m->triggerIns.triggerTick[0][1] == 1);
}

TEST_CASE("JSON round-trip preserves panelTheme and script", "[MidiKit]") {
	Kit<> kit;
	MidiKitModule* m = kit.m;

	m->panelTheme = 2;
	m->loadScript(LUA_SCRIPT);

	json_t* j = m->dataToJson();

	m->panelTheme = 0;
	m->clearScript();
	REQUIRE(m->host.script == "");

	m->dataFromJson(j);
	json_decref(j);

	REQUIRE(m->panelTheme == 2);
	REQUIRE(m->host.script == LUA_SCRIPT);
	REQUIRE(m->host.isLuaEngine());
}

TEST_CASE("process() does not crash with Lua script loaded", "[MidiKit]") {
	Kit<> kit;
	MidiKitModule* m = kit.m;

	m->loadScript(LUA_SCRIPT);

	for (int i = 0; i < 20; i++) {
		REQUIRE_NOTHROW(m->process(Test::makeProcessArgs(i + 1)));
	}
}
