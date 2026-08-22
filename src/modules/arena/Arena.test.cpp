#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"

#include "Arena.cpp"

using namespace StoermelderPackOne::Arena;

typedef ArenaModule<8, 4> MODULE;
static const int IN_PORTS = 8;
static const int MIX_PORTS = 4;

SYNC_MODEL(modelArena, "Arena");
Test::TestContext<> testContext;


// Helpers

// Set the effective position of an IN port by loading filters and params directly.
static void setInPosition(MODULE* m, int j, float x, float y) {
	m->inputUiX[j] = x;
	m->inputXfilter[j].out = x;
	m->inputUiY[j] = y;
	m->inputYfilter[j].out = y;
	m->params[MODULE::IN_X_POS + j].setValue(x);
	m->params[MODULE::IN_Y_POS + j].setValue(y);
}

// Set the effective position of a MIX port.
static void setMixPosition(MODULE* m, int i, float x, float y) {
	m->mixUiX[i] = x;
	m->mixXfilter[i].out = x;
	m->mixUiY[i] = y;
	m->mixYfilter[i].out = y;
	m->params[MODULE::MIX_X_POS + i].setValue(x);
	m->params[MODULE::MIX_Y_POS + i].setValue(y);
}

// Set an IN port's radius filter and stored value.
static void setRadius(MODULE* m, int j, float r) {
	m->radiusUi[j] = r;
	m->radiusFilter[j].out = r;
}


TEST_CASE("Construction and initialization", "[Arena]") {
	MODULE* m = Test::createModule<MODULE>("Arena");
	ArenaWidget* mw = Test::createWidget<ArenaWidget>("Arena");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);

	Test::destroyWidget(mw);
	Test::destroyModule(m);
}

TEST_CASE("Preset JSON null-guards", "[Arena][JSON]") {
	auto module = Test::createModule<ArenaModule<8, 4>>("Arena");

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

TEST_CASE("JSON round-trip preserves module state", "[Arena]") {
	auto* m = Test::createModule<MODULE>("Arena");

	m->panelTheme = 1;
	m->inportsUsed = 5;
	m->mixportsUsed = 3;

	// Distinctive values across ALL ports, not just the first few entries
	const MODMODE modModes[IN_PORTS] = {MODMODE::RADIUS, MODMODE::AMOUNT, MODMODE::OFFSET_X, MODMODE::OFFSET_Y,
		MODMODE::WALK, MODMODE::OFFSET_Y, MODMODE::OFFSET_X, MODMODE::AMOUNT};
	const OUTPUTMODE outModes[IN_PORTS] = {OUTPUTMODE::SCALE, OUTPUTMODE::LIMIT, OUTPUTMODE::CLIP_UNI, OUTPUTMODE::CLIP_BI,
		OUTPUTMODE::FOLD_UNI, OUTPUTMODE::FOLD_BI, OUTPUTMODE::LIMIT, OUTPUTMODE::SCALE};
	for (int j = 0; j < IN_PORTS; j++) {
		m->modMode[j] = modModes[j];
		m->outputMode[j] = outModes[j];
		m->inputXBipolar[j] = j % 2 == 0;
		m->inputYBipolar[j] = j % 3 == 0;
		// radius/amount persist via the ENGINE-final arrays (scGetRadiusFinal);
		// loading restores them into the UI arrays and process() re-derives
		// the final arrays from those, so set both sides deterministically.
		setRadius(m, j, 0.15f * (j + 1));
		m->scSetRadiusFinal(j, 0.15f * (j + 1));
		m->scSetAmountImmediate(j, 0.2f + 0.1f * j);
		m->scSetAmountFinal(j, 0.2f + 0.1f * j);
	}
	for (int i = 0; i < MIX_PORTS; i++) {
		m->mixportXBipolar[i] = i % 2 == 1;
		m->mixportYBipolar[i] = i >= 2;
		// Nested per-port sequence state (Seq::dataToJson)
		m->seqSelected[i] = (i + 2) % StoermelderPackOne::XYSEQ_COUNT;
		m->seqMode[i] = (i % 2 == 0) ? StoermelderPackOne::XYSEQ_MODE::TRIG_FWD : StoermelderPackOne::XYSEQ_MODE::TRIG_REV;
		m->seqInterpolate[i] = (i < 2) ? StoermelderPackOne::XYSEQ_INTERPOLATE::LINEAR : StoermelderPackOne::XYSEQ_INTERPOLATE::CUBIC;
		m->seqData[i][i].length = 3;
		m->seqData[i][i].x[0] = 0.1f * i; m->seqData[i][i].y[0] = 0.2f * i;
		m->seqData[i][i].x[1] = 0.3f * i; m->seqData[i][i].y[1] = 0.4f * i;
		m->seqData[i][i].x[2] = 0.5f * i; m->seqData[i][i].y[2] = 0.6f * i;
	}

	json_t* j = m->dataToJson();

	auto* m2 = Test::createModule<MODULE>("Arena");
	m2->dataFromJson(j);
	json_decref(j);

	SECTION("Scalars") {
		REQUIRE(m2->panelTheme == 1);
		REQUIRE(m2->inportsUsed == 5);
		REQUIRE(m2->mixportsUsed == 3);
	}

	SECTION("IN ports: modes and bipolar flags") {
		for (int j = 0; j < IN_PORTS; j++) {
			REQUIRE(m2->modMode[j] == modModes[j]);
			REQUIRE(m2->outputMode[j] == outModes[j]);
			REQUIRE(m2->inputXBipolar[j] == (j % 2 == 0));
			REQUIRE(m2->inputYBipolar[j] == (j % 3 == 0));
		}
	}

	SECTION("IN ports: radius and amount") {
		for (int j = 0; j < IN_PORTS; j++) {
			// Restored into the UI arrays immediately...
			REQUIRE(m2->radiusUi[j] == Catch::Approx(0.15f * (j + 1)));
			REQUIRE(m2->amountUi[j] == Catch::Approx(0.2f + 0.1f * j));
		}
		// ...and re-derived into the engine-final arrays on the next tick,
		// but only for ACTIVE ports (j < inportsUsed)
		m2->process(Test::makeProcessArgs(1));
		for (int j = 0; j < m2->inportsUsed; j++) {
			REQUIRE(m2->scGetRadiusFinal(j) == Catch::Approx(0.15f * (j + 1)));
			REQUIRE(m2->scGetAmountFinal(j) == Catch::Approx(0.2f + 0.1f * j));
		}
	}

	SECTION("MIX ports: bipolar flags") {
		for (int i = 0; i < MIX_PORTS; i++) {
			REQUIRE(m2->mixportXBipolar[i] == (i % 2 == 1));
			REQUIRE(m2->mixportYBipolar[i] == (i >= 2));
		}
	}

	SECTION("MIX ports: sequence state") {
		for (int i = 0; i < MIX_PORTS; i++) {
			REQUIRE(m2->seqSelected[i] == (i + 2) % StoermelderPackOne::XYSEQ_COUNT);
			REQUIRE(m2->seqMode[i] == ((i % 2 == 0) ? StoermelderPackOne::XYSEQ_MODE::TRIG_FWD : StoermelderPackOne::XYSEQ_MODE::TRIG_REV));
			REQUIRE(m2->seqInterpolate[i] == ((i < 2) ? StoermelderPackOne::XYSEQ_INTERPOLATE::LINEAR : StoermelderPackOne::XYSEQ_INTERPOLATE::CUBIC));
			REQUIRE(m2->seqData[i][i].length == 3);
			REQUIRE(m2->seqData[i][i].x[0] == Catch::Approx(0.1f * i));
			REQUIRE(m2->seqData[i][i].y[0] == Catch::Approx(0.2f * i));
			REQUIRE(m2->seqData[i][i].x[2] == Catch::Approx(0.5f * i));
			REQUIRE(m2->seqData[i][i].y[2] == Catch::Approx(0.6f * i));
		}
	}

	Test::destroyModule(m);
	Test::destroyModule(m2);
}



// scGetDistance: mirrors TransitPad's dist[]/weight coverage. Arena is the
// higher-risk consumer for the XyScreenNodes/XyScreenCursor refactor because
// MIX_PORTS > 1 exercises cursor (type-1) ids >= 1, which TransitPad's single
// Out cursor never does.

TEST_CASE("scGetDistance returns the per-(mixport,inport) distance", "[Arena]") {
	auto* m = Test::createModule<MODULE>("Arena");

	setInPosition(m, 0, 0.f, 0.f);
	setMixPosition(m, 0, 0.f, 0.f);
	setMixPosition(m, 1, 3.f, 4.f);
	// A radius large enough that the process() loop actually writes dist[][]
	// for both mixports (dist is only computed up to inportsUsed).
	setRadius(m, 0, 100.f);
	m->inportsUsed = 1;

	m->process(Test::makeProcessArgs(1));

	// MIX-0 co-located with IN-0
	REQUIRE(m->scGetDistance(1, 0, 0, 0) == Catch::Approx(0.f).margin(0.001f));
	// MIX-1 at (3,4) from IN-0 at (0,0): classic 3-4-5 triangle
	REQUIRE(m->scGetDistance(1, 1, 0, 0) == Catch::Approx(5.f).margin(0.001f));

	Test::destroyModule(m);
}

TEST_CASE("scGetDistance ignores its source-type/dest-type arguments", "[Arena]") {
	// Per the refactor plan (XyScreenNodes_refactor_plan.md §3), the 4-argument
	// scGetDistance is only ever called as scGetDistance(1, cursorId, 0, nodeId)
	// from the cursor widget, and both implementations ignore typeSource/typeDest
	// entirely. Confirm that empirically before the refactor collapses the
	// signature to getCursorToNodeDistance(cursorId, nodeId).
	auto* m = Test::createModule<MODULE>("Arena");

	setInPosition(m, 0, 0.f, 0.f);
	setMixPosition(m, 0, 6.f, 8.f);
	setRadius(m, 0, 100.f);
	m->inportsUsed = 1;

	m->process(Test::makeProcessArgs(1));

	float withDeclaredTypes = m->scGetDistance(1, 0, 0, 0);
	float withBogusTypes = m->scGetDistance(99, 0, 42, 0);

	REQUIRE(withDeclaredTypes == Catch::Approx(10.f).margin(0.001f));
	REQUIRE(withBogusTypes == withDeclaredTypes);

	Test::destroyModule(m);
}

TEST_CASE("MIX id >= 1 correctly indexes its own distance/weight, independent of MIX-0", "[Arena]") {
	// The refactor plan flags this as the concrete case where the current
	// INPUTS-bound guard would silently misbehave for cursor ids >= 1 once
	// cursor storage moves out of XyScreenModule (§1c). Lock down today's
	// correct behaviour before that move.
	auto* m = Test::createModule<MODULE>("Arena");

	setInPosition(m, 0, 0.5f, 0.5f);
	setMixPosition(m, 0, 0.5f, 0.5f);   // co-located with IN-0: inside radius
	setMixPosition(m, 1, 0.f, 0.f);     // far from IN-0: outside radius
	setRadius(m, 0, 0.2f);
	m->inportsUsed = 1;

	m->inputs[MODULE::IN + 0].channels = 1;
	m->inputs[MODULE::IN + 0].setVoltage(5.f);

	m->process(Test::makeProcessArgs(1));

	REQUIRE(m->outputs[MODULE::MIX_OUTPUT + 0].getVoltage() > 0.f);
	REQUIRE(m->outputs[MODULE::MIX_OUTPUT + 1].getVoltage() == Catch::Approx(0.f).margin(0.001f));

	Test::destroyModule(m);
}

// Sc::dataToJson()/dataFromJson() write "radius"/"amount" for type 0 (nodes)
// only, and silently no-op for type 1 (cursor/MIX ports) today. This test
// pins the exact JSON produced for a distinctive node state so any refactor
// that moves or renames those keys fails loudly. It also proves empirically
// (rather than by reading the "if (type == 0)" guard) that the type-1 calls
// write no "radius"/"amount" keys into "mixports" entries, which is the fact
// Stage 3 of the plan needs verified before it can delete those calls.

TEST_CASE("Golden JSON: node (IN port) radius/amount round-trip byte-identically", "[Arena][JSON]") {
	auto* m = Test::createModule<MODULE>("Arena");

	m->scSetRadiusImmediate(0, 0.125f);
	m->scSetRadiusFinal(0, 0.125f);
	m->scSetAmountImmediate(0, 0.875f);
	m->scSetAmountFinal(0, 0.875f);

	json_t* dataJ = json_object();
	m->Sc::dataToJson(dataJ, 0, 0);

	char* dumped = json_dumps(dataJ, JSON_SORT_KEYS | JSON_COMPACT | JSON_REAL_PRECISION(9));
	std::string actual(dumped);
	free(dumped);
	json_decref(dataJ);

	REQUIRE(actual == "{\"amount\":0.875,\"radius\":0.125}");

	Test::destroyModule(m);
}

TEST_CASE("Golden JSON: cursor (MIX port) dataToJson writes no keys", "[Arena][JSON]") {
	auto* m = Test::createModule<MODULE>("Arena");

	setMixPosition(m, 1, 0.3f, 0.7f);

	json_t* dataJ = json_object();
	m->Sc::dataToJson(dataJ, 1, 1);

	REQUIRE(json_object_size(dataJ) == 0);

	json_decref(dataJ);
	Test::destroyModule(m);
}

TEST_CASE("Golden JSON: cursor (MIX port) dataFromJson on an empty object is a no-op self-assignment", "[Arena][JSON]") {
	// Sc::dataFromJson(dataJ, 1, id) unconditionally calls
	// scSetXyImmediate(1, id, scGetXFinal(1, id), scGetYFinal(1, id)) — reading
	// the MIX port's own current position and writing it straight back. Confirm
	// that round-trips the position exactly rather than zeroing or perturbing it.
	auto* m = Test::createModule<MODULE>("Arena");

	setMixPosition(m, 1, 0.3f, 0.7f);
	float xBefore = m->params[MODULE::MIX_X_POS + 1].getValue();
	float yBefore = m->params[MODULE::MIX_Y_POS + 1].getValue();

	json_t* dataJ = json_object();
	m->Sc::dataFromJson(dataJ, 1, 1);
	json_decref(dataJ);

	REQUIRE(m->params[MODULE::MIX_X_POS + 1].getValue() == Catch::Approx(xBefore));
	REQUIRE(m->params[MODULE::MIX_Y_POS + 1].getValue() == Catch::Approx(yBefore));

	Test::destroyModule(m);
}

TEST_CASE("Golden JSON: full module dataToJson is byte-identical for a distinctive state", "[Arena][JSON]") {
	auto* m = Test::createModule<MODULE>("Arena");

	m->panelTheme = 0;
	m->inportsUsed = 1;
	m->mixportsUsed = 1;

	m->scSetRadiusImmediate(0, 0.25f);
	m->scSetRadiusFinal(0, 0.25f);
	m->scSetAmountImmediate(0, 0.5f);
	m->scSetAmountFinal(0, 0.5f);
	m->modMode[0] = MODMODE::RADIUS;
	m->outputMode[0] = OUTPUTMODE::CLIP_BI;
	m->inputXBipolar[0] = true;
	m->inputYBipolar[0] = false;
	m->mixportXBipolar[0] = false;
	m->mixportYBipolar[0] = true;

	json_t* rootJ = m->dataToJson();
	json_t* inportsJ = json_object_get(rootJ, "inports");
	json_t* inport0J = json_array_get(inportsJ, 0);
	json_t* mixportsJ = json_object_get(rootJ, "mixports");
	json_t* mixport0J = json_array_get(mixportsJ, 0);

	char* inportDumped = json_dumps(inport0J, JSON_SORT_KEYS | JSON_COMPACT | JSON_REAL_PRECISION(9));
	std::string inportActual(inportDumped);
	free(inportDumped);

	REQUIRE(inportActual == "{\"amount\":0.5,\"inputXBipolar\":true,\"inputYBipolar\":false,\"modMode\":0,\"outputMode\":3,\"radius\":0.25}");

	// mixport entries never carry "radius"/"amount" — only the module-owned
	// bipolar flags and the (empty) Seq state.
	REQUIRE(json_object_get(mixport0J, "radius") == nullptr);
	REQUIRE(json_object_get(mixport0J, "amount") == nullptr);

	json_decref(rootJ);
	Test::destroyModule(m);
}


// Proximity mixing: MIX output

TEST_CASE("MIX output is non-zero when IN is within radius", "[Arena]") {
	auto* m = Test::createModule<MODULE>("Arena");

	// Place IN-0 and MIX-0 at the same point
	setInPosition(m, 0, 0.5f, 0.5f);
	setMixPosition(m, 0, 0.5f, 0.5f);
	// Ensure large radius so IN-0 is well inside
	setRadius(m, 0, 1.0f);

	m->inputs[MODULE::IN + 0].channels = 1;
	m->inputs[MODULE::IN + 0].setVoltage(5.f);

	m->process(Test::makeProcessArgs(1));

	REQUIRE(m->outputs[MODULE::MIX_OUTPUT + 0].getVoltage() > 0.f);

	Test::destroyModule(m);
}

TEST_CASE("MIX output is zero when IN is outside radius", "[Arena]") {
	auto* m = Test::createModule<MODULE>("Arena");

	// Place IN-0 at one corner and MIX-0 at the opposite – distance ≈ 1.41
	setInPosition(m, 0, 0.f, 0.f);
	setMixPosition(m, 0, 1.f, 1.f);
	setRadius(m, 0, 0.5f);

	m->inputs[MODULE::IN + 0].channels = 1;
	m->inputs[MODULE::IN + 0].setVoltage(5.f);

	m->process(Test::makeProcessArgs(1));

	REQUIRE(m->outputs[MODULE::MIX_OUTPUT + 0].getVoltage() == Catch::Approx(0.f));

	Test::destroyModule(m);
}

TEST_CASE("MIX output sums contributions from multiple IN ports", "[Arena]") {
	auto* m = Test::createModule<MODULE>("Arena");

	// Both IN-0 and IN-1 overlap with MIX-0
	setInPosition(m, 0, 0.5f, 0.5f);
	setInPosition(m, 1, 0.5f, 0.5f);
	setMixPosition(m, 0, 0.5f, 0.5f);
	setRadius(m, 0, 1.0f);
	setRadius(m, 1, 1.0f);

	m->inputs[MODULE::IN + 0].channels = 1;
	m->inputs[MODULE::IN + 0].setVoltage(3.f);
	m->inputs[MODULE::IN + 1].channels = 1;
	m->inputs[MODULE::IN + 1].setVoltage(3.f);

	m->process(Test::makeProcessArgs(1));

	// Both at same point: s=1.0 each → mix = 1*3 + 1*3 = 6V
	REQUIRE(m->outputs[MODULE::MIX_OUTPUT + 0].getVoltage() == Catch::Approx(6.0f).margin(0.1f));

	Test::destroyModule(m);
}


// OUT_OUTPUT with SCALE mode

TEST_CASE("OUT_OUTPUT SCALE mode scales by outNorm / MIX_PORTS", "[Arena]") {
	auto* m = Test::createModule<MODULE>("Arena");

	// IN-0 and MIX-0 are co-located; MIX 1-3 are at (0,0), ~0.707 away.
	// Radius 0.3 covers only MIX-0 (dist=0) and excludes the others (dist≈0.707).
	setInPosition(m, 0, 0.5f, 0.5f);
	setMixPosition(m, 0, 0.5f, 0.5f);
	setMixPosition(m, 1, 0.0f, 0.0f);
	setMixPosition(m, 2, 0.0f, 0.0f);
	setMixPosition(m, 3, 0.0f, 0.0f);
	setRadius(m, 0, 0.3f);

	m->outputMode[0] = OUTPUTMODE::SCALE;

	m->inputs[MODULE::IN + 0].channels = 1;
	m->inputs[MODULE::IN + 0].setVoltage(8.f);
	// Enable OUT output (must be "connected" for the output branch to run)
	m->outputs[MODULE::OUT_OUTPUT + 0].channels = 1;

	m->process(Test::makeProcessArgs(1));

	// IN-0 at (0.5,0.5), MIX-0 at (0.5,0.5): dist=0, r=0.3
	// s = min(1, (0.3-0)/0.3 * 1.1) = 1.0 → outNorm[0] = 1.0
	// SCALE: v * outNorm / MIX_PORTS = 8 * 1 / 4 = 2.0
	float v = m->outputs[MODULE::OUT_OUTPUT + 0].getVoltage();
	REQUIRE(v == Catch::Approx(2.0f).margin(0.05f));

	Test::destroyModule(m);
}


// OUT_OUTPUT with LIMIT mode

TEST_CASE("OUT_OUTPUT LIMIT mode caps scaling at 1x", "[Arena]") {
	auto* m = Test::createModule<MODULE>("Arena");

	setInPosition(m, 0, 0.5f, 0.5f);
	setMixPosition(m, 0, 0.5f, 0.5f);
	setRadius(m, 0, 1.0f);

	m->outputMode[0] = OUTPUTMODE::LIMIT;

	m->inputs[MODULE::IN + 0].channels = 1;
	m->inputs[MODULE::IN + 0].setVoltage(5.f);
	m->outputs[MODULE::OUT_OUTPUT + 0].channels = 1;

	m->process(Test::makeProcessArgs(1));

	// outNorm[0] = 1.0; LIMIT: v * min(outNorm, 1) = 5 * 1 = 5
	REQUIRE(m->outputs[MODULE::OUT_OUTPUT + 0].getVoltage() == Catch::Approx(5.f).margin(0.05f));

	Test::destroyModule(m);
}

TEST_CASE("OUT_OUTPUT LIMIT output is zero when IN is outside all MIX radii", "[Arena]") {
	auto* m = Test::createModule<MODULE>("Arena");

	// All MIX ports far away → outNorm = 0
	setInPosition(m, 0, 0.5f, 0.5f);
	for (int i = 0; i < MIX_PORTS; i++) {
		setMixPosition(m, i, 0.0f, 0.0f);
	}
	setRadius(m, 0, 0.3f);

	m->outputMode[0] = OUTPUTMODE::LIMIT;

	m->inputs[MODULE::IN + 0].channels = 1;
	m->inputs[MODULE::IN + 0].setVoltage(5.f);
	m->outputs[MODULE::OUT_OUTPUT + 0].channels = 1;

	m->process(Test::makeProcessArgs(1));

	REQUIRE(m->outputs[MODULE::OUT_OUTPUT + 0].getVoltage() == Catch::Approx(0.f).margin(0.001f));

	Test::destroyModule(m);
}


// OUT_OUTPUT CLIP modes

TEST_CASE("OUT_OUTPUT CLIP_UNI mode clamps output to 0..10V", "[Arena]") {
	auto* m = Test::createModule<MODULE>("Arena");

	setInPosition(m, 0, 0.5f, 0.5f);
	setMixPosition(m, 0, 0.5f, 0.5f);
	setRadius(m, 0, 1.0f);

	m->outputMode[0] = OUTPUTMODE::CLIP_UNI;

	// A negative input should be clipped to 0
	m->inputs[MODULE::IN + 0].channels = 1;
	m->inputs[MODULE::IN + 0].setVoltage(-5.f);
	m->outputs[MODULE::OUT_OUTPUT + 0].channels = 1;

	m->process(Test::makeProcessArgs(1));

	REQUIRE(m->outputs[MODULE::OUT_OUTPUT + 0].getVoltage() == Catch::Approx(0.f).margin(0.001f));

	Test::destroyModule(m);
}

TEST_CASE("OUT_OUTPUT CLIP_BI mode clamps output to -5..5V", "[Arena]") {
	auto* m = Test::createModule<MODULE>("Arena");

	setInPosition(m, 0, 0.5f, 0.5f);
	setMixPosition(m, 0, 0.5f, 0.5f);
	setRadius(m, 0, 1.0f);

	m->outputMode[0] = OUTPUTMODE::CLIP_BI;

	// Large positive input should be clipped to 5V
	m->inputs[MODULE::IN + 0].channels = 1;
	m->inputs[MODULE::IN + 0].setVoltage(10.f);
	m->outputs[MODULE::OUT_OUTPUT + 0].channels = 1;

	// outNorm will be ~1.1 (> 1) at dist=0, so v * outNorm > 5 → clamped to 5
	m->process(Test::makeProcessArgs(1));

	REQUIRE(m->outputs[MODULE::OUT_OUTPUT + 0].getVoltage() == Catch::Approx(5.f).margin(0.1f));

	Test::destroyModule(m);
}


// OUT_OUTPUT FOLD modes

TEST_CASE("OUT_OUTPUT FOLD_UNI mode folds signal instead of clipping", "[Arena]") {
	auto* m = Test::createModule<MODULE>("Arena");

	// MIX-0 and MIX-1 both at IN-0's position → outNorm = 2.0
	// MIX-2 and MIX-3 at (0,0), dist≈0.707 > radius 0.3 → no contribution
	setInPosition(m, 0, 0.5f, 0.5f);
	setMixPosition(m, 0, 0.5f, 0.5f);
	setMixPosition(m, 1, 0.5f, 0.5f);
	setMixPosition(m, 2, 0.0f, 0.0f);
	setMixPosition(m, 3, 0.0f, 0.0f);
	setRadius(m, 0, 0.3f);

	m->outputMode[0] = OUTPUTMODE::FOLD_UNI;

	m->inputs[MODULE::IN + 0].channels = 1;
	m->inputs[MODULE::IN + 0].setVoltage(7.5f);
	m->outputs[MODULE::OUT_OUTPUT + 0].channels = 1;

	m->process(Test::makeProcessArgs(1));

	// v = clamp(7.5, 0, 10)/10 * 2.0 = 1.5 → fold: intf=1 (odd), frac=0.5 → (1-0.5)*10 = 5V
	// CLIP_UNI would give clamp(7.5*2, 0, 10) = 10V — fold produces a different result
	REQUIRE(m->outputs[MODULE::OUT_OUTPUT + 0].getVoltage() == Catch::Approx(5.0f).margin(0.05f));

	Test::destroyModule(m);
}

TEST_CASE("OUT_OUTPUT FOLD_BI mode folds signal instead of clipping", "[Arena]") {
	auto* m = Test::createModule<MODULE>("Arena");

	// Same outNorm=2.0 setup as FOLD_UNI test above
	setInPosition(m, 0, 0.5f, 0.5f);
	setMixPosition(m, 0, 0.5f, 0.5f);
	setMixPosition(m, 1, 0.5f, 0.5f);
	setMixPosition(m, 2, 0.0f, 0.0f);
	setMixPosition(m, 3, 0.0f, 0.0f);
	setRadius(m, 0, 0.3f);

	m->outputMode[0] = OUTPUTMODE::FOLD_BI;

	m->inputs[MODULE::IN + 0].channels = 1;
	m->inputs[MODULE::IN + 0].setVoltage(5.f);
	m->outputs[MODULE::OUT_OUTPUT + 0].channels = 1;

	m->process(Test::makeProcessArgs(1));

	// v = clamp(5, -5, 5)/5 * 2.0 = 2.0 → fold: intf=2 (even), frac=0 → 0 * 5 = 0V
	// CLIP_BI would give clamp(5*2, -5, 5) = 5V — fold wraps back to 0
	REQUIRE(m->outputs[MODULE::OUT_OUTPUT + 0].getVoltage() == Catch::Approx(0.0f).margin(0.05f));

	Test::destroyModule(m);
}


// CV input controlling position

TEST_CASE("IN_X_INPUT CV moves the IN port x-position", "[Arena]") {
	auto* m = Test::createModule<MODULE>("Arena");

	// Full-width attenuverter, unipolar 0-10V input → x mapped 0-1
	m->params[MODULE::IN_X_PARAM + 0].setValue(1.f);
	m->inputXBipolar[0] = false;

	m->inputs[MODULE::IN_X_INPUT + 0].channels = 1;
	m->inputs[MODULE::IN_X_INPUT + 0].setVoltage(5.f); // → x = 5/10 = 0.5

	m->process(Test::makeProcessArgs(1));

	REQUIRE(m->params[MODULE::IN_X_POS + 0].getValue() == Catch::Approx(0.5f).margin(0.01f));

	Test::destroyModule(m);
}

TEST_CASE("IN_Y_INPUT CV moves the IN port y-position", "[Arena]") {
	auto* m = Test::createModule<MODULE>("Arena");

	m->params[MODULE::IN_Y_PARAM + 0].setValue(1.f);
	m->inputYBipolar[0] = false;

	m->inputs[MODULE::IN_Y_INPUT + 0].channels = 1;
	m->inputs[MODULE::IN_Y_INPUT + 0].setVoltage(7.f); // → y = 7/10 = 0.7

	m->process(Test::makeProcessArgs(1));

	REQUIRE(m->params[MODULE::IN_Y_POS + 0].getValue() == Catch::Approx(0.7f).margin(0.01f));

	Test::destroyModule(m);
}

TEST_CASE("inputXBipolar adds 5V offset to X CV", "[Arena]") {
	auto* m = Test::createModule<MODULE>("Arena");

	m->params[MODULE::IN_X_PARAM + 0].setValue(1.f);
	m->inputXBipolar[0] = true;

	m->inputs[MODULE::IN_X_INPUT + 0].channels = 1;
	m->inputs[MODULE::IN_X_INPUT + 0].setVoltage(0.f); // + 5V bias → x = 5/10 = 0.5

	m->process(Test::makeProcessArgs(1));

	REQUIRE(m->params[MODULE::IN_X_POS + 0].getValue() == Catch::Approx(0.5f).margin(0.01f));

	Test::destroyModule(m);
}


// inportsUsed / mixportsUsed

TEST_CASE("inportsUsed limits which IN ports are processed", "[Arena]") {
	auto* m = Test::createModule<MODULE>("Arena");

	// Signal on IN-1 which is beyond the active count
	m->inportsUsed = 1;

	setInPosition(m, 1, 0.5f, 0.5f);
	setMixPosition(m, 0, 0.5f, 0.5f);
	setRadius(m, 1, 1.0f);

	m->inputs[MODULE::IN + 1].channels = 1;
	m->inputs[MODULE::IN + 1].setVoltage(5.f);

	m->process(Test::makeProcessArgs(1));

	// IN-1 should be ignored; MIX-0 output should be 0
	REQUIRE(m->outputs[MODULE::MIX_OUTPUT + 0].getVoltage() == Catch::Approx(0.f).margin(0.001f));

	Test::destroyModule(m);
}

TEST_CASE("mixportsUsed limits which MIX ports produce output", "[Arena]") {
	auto* m = Test::createModule<MODULE>("Arena");

	m->mixportsUsed = 1;

	setInPosition(m, 0, 0.5f, 0.5f);
	setMixPosition(m, 1, 0.5f, 0.5f);
	setRadius(m, 0, 1.0f);

	m->inputs[MODULE::IN + 0].channels = 1;
	m->inputs[MODULE::IN + 0].setVoltage(5.f);

	m->process(Test::makeProcessArgs(1));

	// MIX-1 is outside the active count; should produce nothing
	REQUIRE(m->outputs[MODULE::MIX_OUTPUT + 1].getVoltage() == Catch::Approx(0.f).margin(0.001f));

	Test::destroyModule(m);
}


// Modulation modes

TEST_CASE("MODMODE::RADIUS: MOD input controls effective radius", "[Arena]") {
	auto* m = Test::createModule<MODULE>("Arena");

	m->modMode[0] = MODMODE::RADIUS;

	setInPosition(m, 0, 0.5f, 0.5f);
	setMixPosition(m, 0, 0.5f, 0.5f);

	// MOD input at 10V with attenuverter 1.0 → radius = clamp(10/10, 0, 1) = 1.0
	m->params[MODULE::MOD_PARAM + 0].setValue(1.f);
	m->inputs[MODULE::MOD_INPUT + 0].channels = 1;
	m->inputs[MODULE::MOD_INPUT + 0].setVoltage(10.f);

	m->inputs[MODULE::IN + 0].channels = 1;
	m->inputs[MODULE::IN + 0].setVoltage(5.f);

	m->process(Test::makeProcessArgs(1));

	// dist=0, radius=1.0 → within range → output > 0
	REQUIRE(m->outputs[MODULE::MIX_OUTPUT + 0].getVoltage() > 0.f);
	// radius was set to 1.0 via mod input
	REQUIRE(m->scGetRadiusFinal(0) == Catch::Approx(1.0f).margin(0.01f));

	Test::destroyModule(m);
}

TEST_CASE("MODMODE::RADIUS: zero MOD input collapses radius to zero", "[Arena]") {
	auto* m = Test::createModule<MODULE>("Arena");

	m->modMode[0] = MODMODE::RADIUS;

	setInPosition(m, 0, 0.5f, 0.5f);
	setMixPosition(m, 0, 0.5f, 0.5f);

	// MOD = 0V → radius=0 → IN outside every possible radius → no contribution
	m->params[MODULE::MOD_PARAM + 0].setValue(1.f);
	m->inputs[MODULE::MOD_INPUT + 0].channels = 1;
	m->inputs[MODULE::MOD_INPUT + 0].setVoltage(0.f);

	m->inputs[MODULE::IN + 0].channels = 1;
	m->inputs[MODULE::IN + 0].setVoltage(5.f);

	m->process(Test::makeProcessArgs(1));

	REQUIRE(m->outputs[MODULE::MIX_OUTPUT + 0].getVoltage() == Catch::Approx(0.f).margin(0.001f));

	Test::destroyModule(m);
}

TEST_CASE("MODMODE::AMOUNT: MOD input controls signal amount", "[Arena]") {
	auto* m = Test::createModule<MODULE>("Arena");

	m->modMode[0] = MODMODE::AMOUNT;

	setInPosition(m, 0, 0.5f, 0.5f);
	setMixPosition(m, 0, 0.5f, 0.5f);
	setRadius(m, 0, 1.0f);

	m->params[MODULE::MOD_PARAM + 0].setValue(1.f);

	// Full amount (MOD = 10V)
	m->inputs[MODULE::MOD_INPUT + 0].channels = 1;
	m->inputs[MODULE::MOD_INPUT + 0].setVoltage(10.f);

	m->inputs[MODULE::IN + 0].channels = 1;
	m->inputs[MODULE::IN + 0].setVoltage(5.f);

	m->process(Test::makeProcessArgs(1));
	float fullOutput = m->outputs[MODULE::MIX_OUTPUT + 0].getVoltage();

	// Half amount (MOD = 5V)
	m->inputs[MODULE::MOD_INPUT + 0].setVoltage(5.f);
	m->process(Test::makeProcessArgs(2));
	float halfOutput = m->outputs[MODULE::MIX_OUTPUT + 0].getVoltage();

	REQUIRE(fullOutput > 0.f);
	REQUIRE(halfOutput > 0.f);
	REQUIRE(fullOutput > halfOutput);

	Test::destroyModule(m);
}

TEST_CASE("MODMODE::OFFSET_X: MOD input shifts IN-port x position", "[Arena]") {
	auto* m = Test::createModule<MODULE>("Arena");

	m->modMode[0] = MODMODE::OFFSET_X;

	// Tight radius: without the offset, IN-0 at x=0.5 is outside MIX-0 at x=0.8 (dist=0.3 > r=0.05).
	// 3V → offset = clamp(3/10, -1, 1) = 0.3 → new IN x = 0.8, exactly on MIX-0.
	setInPosition(m, 0, 0.5f, 0.5f);
	setMixPosition(m, 0, 0.8f, 0.5f);
	setRadius(m, 0, 0.05f);

	m->params[MODULE::MOD_PARAM + 0].setValue(1.f);
	m->inputs[MODULE::MOD_INPUT + 0].channels = 1;
	m->inputs[MODULE::MOD_INPUT + 0].setVoltage(3.f);

	m->inputs[MODULE::IN + 0].channels = 1;
	m->inputs[MODULE::IN + 0].setVoltage(5.f);

	m->process(Test::makeProcessArgs(1));

	REQUIRE(m->params[MODULE::IN_X_POS + 0].getValue() == Catch::Approx(0.8f).margin(0.01f));
	REQUIRE(m->outputs[MODULE::MIX_OUTPUT + 0].getVoltage() > 0.f);

	Test::destroyModule(m);
}


// MIX CV position control

TEST_CASE("MODMODE::OFFSET_Y: MOD input shifts IN-port y position", "[Arena]") {
	auto* m = Test::createModule<MODULE>("Arena");

	m->modMode[0] = MODMODE::OFFSET_Y;

	// Place IN-0 at y=0.5; MIX-0 at y=0.8
	setInPosition(m, 0, 0.5f, 0.5f);
	setMixPosition(m, 0, 0.5f, 0.8f);
	setRadius(m, 0, 1.0f);

	m->params[MODULE::MOD_PARAM + 0].setValue(1.f);
	m->inputs[MODULE::MOD_INPUT + 0].channels = 1;
	// 3V → offset = clamp(3/10, -1, 1) = 0.3 → new IN y ≈ 0.8
	m->inputs[MODULE::MOD_INPUT + 0].setVoltage(3.f);

	m->inputs[MODULE::IN + 0].channels = 1;
	m->inputs[MODULE::IN + 0].setVoltage(5.f);

	m->process(Test::makeProcessArgs(1));

	// IN y was offset from 0.5 to 0.8, closer to MIX-0
	REQUIRE(m->outputs[MODULE::MIX_OUTPUT + 0].getVoltage() > 0.f);

	// Verify offset applied to y position
	REQUIRE(m->params[MODULE::IN_Y_POS + 0].getValue() == Catch::Approx(0.8f).margin(0.01f));

	Test::destroyModule(m);
}

TEST_CASE("MODMODE::OFFSET_Y does not affect x position", "[Arena]") {
	auto* m = Test::createModule<MODULE>("Arena");

	m->modMode[0] = MODMODE::OFFSET_Y;

	setInPosition(m, 0, 0.3f, 0.5f);
	setMixPosition(m, 0, 0.5f, 0.5f);
	setRadius(m, 0, 1.0f);

	m->params[MODULE::MOD_PARAM + 0].setValue(1.f);
	m->inputs[MODULE::MOD_INPUT + 0].channels = 1;
	m->inputs[MODULE::MOD_INPUT + 0].setVoltage(3.f);

	m->inputs[MODULE::IN + 0].channels = 1;
	m->inputs[MODULE::IN + 0].setVoltage(5.f);

	m->process(Test::makeProcessArgs(1));

	// X position should remain at 0.3 (offset only applies to Y)
	REQUIRE(m->params[MODULE::IN_X_POS + 0].getValue() == Catch::Approx(0.3f).margin(0.01f));

	Test::destroyModule(m);
}


// Edge cases

TEST_CASE("Zero radius produces no MIX output even at same position", "[Arena]") {
	auto* m = Test::createModule<MODULE>("Arena");

	setInPosition(m, 0, 0.5f, 0.5f);
	setMixPosition(m, 0, 0.5f, 0.5f);
	setRadius(m, 0, 0.0f);

	m->inputs[MODULE::IN + 0].channels = 1;
	m->inputs[MODULE::IN + 0].setVoltage(5.f);

	m->process(Test::makeProcessArgs(1));

	REQUIRE(m->outputs[MODULE::MIX_OUTPUT + 0].getVoltage() == Catch::Approx(0.f).margin(0.001f));

	Test::destroyModule(m);
}

TEST_CASE("Zero amount scales IN signal to zero", "[Arena]") {
	auto* m = Test::createModule<MODULE>("Arena");

	setInPosition(m, 0, 0.5f, 0.5f);
	setMixPosition(m, 0, 0.5f, 0.5f);
	setRadius(m, 0, 1.0f);
	m->scSetAmountImmediate(0, 0.0f);
	m->scSetAmountFinal(0, 0.0f);

	m->inputs[MODULE::IN + 0].channels = 1;
	m->inputs[MODULE::IN + 0].setVoltage(5.f);

	m->process(Test::makeProcessArgs(1));

	REQUIRE(m->outputs[MODULE::MIX_OUTPUT + 0].getVoltage() == Catch::Approx(0.f).margin(0.001f));

	Test::destroyModule(m);
}

TEST_CASE("OUT_OUTPUT is zero when no IN cable is connected", "[Arena]") {
	auto* m = Test::createModule<MODULE>("Arena");

	setInPosition(m, 0, 0.5f, 0.5f);
	setMixPosition(m, 0, 0.5f, 0.5f);
	setRadius(m, 0, 1.0f);

	// IN-0 is NOT connected, but OUT-0 is
	m->inputs[MODULE::IN + 0].channels = 0;
	m->outputs[MODULE::OUT_OUTPUT + 0].channels = 1;

	m->process(Test::makeProcessArgs(1));

	REQUIRE(m->outputs[MODULE::OUT_OUTPUT + 0].getVoltage() == Catch::Approx(0.f).margin(0.001f));

	Test::destroyModule(m);
}

TEST_CASE("MIX output is zero when no IN cable is connected", "[Arena]") {
	auto* m = Test::createModule<MODULE>("Arena");

	setInPosition(m, 0, 0.5f, 0.5f);
	setMixPosition(m, 0, 0.5f, 0.5f);
	setRadius(m, 0, 1.0f);

	// No IN cables connected at all
	for (int j = 0; j < IN_PORTS; j++) {
		m->inputs[MODULE::IN + j].channels = 0;
	}

	m->process(Test::makeProcessArgs(1));

	REQUIRE(m->outputs[MODULE::MIX_OUTPUT + 0].getVoltage() == Catch::Approx(0.f).margin(0.001f));

	Test::destroyModule(m);
}


// MIX CV position control


TEST_CASE("MIX_X_INPUT CV moves the MIX port x-position", "[Arena]") {
	auto* m = Test::createModule<MODULE>("Arena");

	// Attenuverter at full, unipolar
	m->params[MODULE::MIX_X_PARAM + 0].setValue(1.f);
	m->mixportXBipolar[0] = false;

	m->inputs[MODULE::MIX_X_INPUT + 0].channels = 1;
	m->inputs[MODULE::MIX_X_INPUT + 0].setVoltage(5.f); // → x = 5/10 * 1.0 = 0.5

	m->process(Test::makeProcessArgs(1));

	REQUIRE(m->params[MODULE::MIX_X_POS + 0].getValue() == Catch::Approx(0.5f).margin(0.01f));

	Test::destroyModule(m);
}

TEST_CASE("MIX_Y_INPUT CV moves the MIX port y-position", "[Arena]") {
	auto* m = Test::createModule<MODULE>("Arena");

	m->params[MODULE::MIX_Y_PARAM + 0].setValue(1.f);
	m->mixportYBipolar[0] = false;

	m->inputs[MODULE::MIX_Y_INPUT + 0].channels = 1;
	m->inputs[MODULE::MIX_Y_INPUT + 0].setVoltage(7.f); // → y = 7/10 * 1.0 = 0.7

	m->process(Test::makeProcessArgs(1));

	REQUIRE(m->params[MODULE::MIX_Y_POS + 0].getValue() == Catch::Approx(0.7f).margin(0.01f));

	Test::destroyModule(m);
}

TEST_CASE("mixportXBipolar adds 0.5 offset to MIX position", "[Arena]") {
	auto* m = Test::createModule<MODULE>("Arena");

	m->params[MODULE::MIX_X_PARAM + 0].setValue(1.f);
	m->mixportXBipolar[0] = true;

	m->inputs[MODULE::MIX_X_INPUT + 0].channels = 1;
	m->inputs[MODULE::MIX_X_INPUT + 0].setVoltage(0.f); // x = 0/10 + 0.5 = 0.5

	m->process(Test::makeProcessArgs(1));

	REQUIRE(m->params[MODULE::MIX_X_POS + 0].getValue() == Catch::Approx(0.5f).margin(0.01f));

	Test::destroyModule(m);
}


// Attenuverter scaling

TEST_CASE("IN_X_PARAM attenuverter at 0.5 halves X CV effect", "[Arena]") {
	auto* m = Test::createModule<MODULE>("Arena");

	m->inputXBipolar[0] = false;

	m->inputs[MODULE::IN_X_INPUT + 0].channels = 1;
	m->inputs[MODULE::IN_X_INPUT + 0].setVoltage(10.f);

	// Full attenuverter → x = 10/10 * 1.0 = 1.0
	m->params[MODULE::IN_X_PARAM + 0].setValue(1.f);
	m->process(Test::makeProcessArgs(1));
	float fullX = m->params[MODULE::IN_X_POS + 0].getValue();

	// Half attenuverter → x = 10/10 * 0.5 = 0.5
	m->params[MODULE::IN_X_PARAM + 0].setValue(0.5f);
	m->process(Test::makeProcessArgs(2));
	float halfX = m->params[MODULE::IN_X_POS + 0].getValue();

	REQUIRE(fullX == Catch::Approx(1.0f).margin(0.01f));
	REQUIRE(halfX == Catch::Approx(0.5f).margin(0.01f));

	Test::destroyModule(m);
}

TEST_CASE("Negative attenuverter inverts X CV", "[Arena]") {
	auto* m = Test::createModule<MODULE>("Arena");

	m->inputXBipolar[0] = false;

	m->inputs[MODULE::IN_X_INPUT + 0].channels = 1;
	m->inputs[MODULE::IN_X_INPUT + 0].setVoltage(8.f);

	// Negative attenuverter → x = 8 * (-1.0) / 10 = -0.8 → clamp(0,1) = 0.0
	m->params[MODULE::IN_X_PARAM + 0].setValue(-1.f);
	m->process(Test::makeProcessArgs(1));

	REQUIRE(m->params[MODULE::IN_X_POS + 0].getValue() == Catch::Approx(0.f).margin(0.01f));

	Test::destroyModule(m);
}

TEST_CASE("MOD attenuverter scales modulation depth", "[Arena]") {
	auto* m = Test::createModule<MODULE>("Arena");

	m->modMode[0] = MODMODE::RADIUS;

	setInPosition(m, 0, 0.5f, 0.5f);
	setMixPosition(m, 0, 0.5f, 0.5f);

	m->inputs[MODULE::MOD_INPUT + 0].channels = 1;
	m->inputs[MODULE::MOD_INPUT + 0].setVoltage(10.f);

	// Full attenuation → radius = clamp(10/10 * 1.0, 0, 1) = 1.0
	m->params[MODULE::MOD_PARAM + 0].setValue(1.f);
	m->inputs[MODULE::IN + 0].channels = 1;
	m->inputs[MODULE::IN + 0].setVoltage(5.f);
	m->process(Test::makeProcessArgs(1));
	float fullRadius = m->scGetRadiusFinal(0);

	// Half attenuation → radius = clamp(10/10 * 0.5, 0, 1) = 0.5
	m->params[MODULE::MOD_PARAM + 0].setValue(0.5f);
	m->process(Test::makeProcessArgs(2));
	float halfRadius = m->scGetRadiusFinal(0);

	REQUIRE(fullRadius == Catch::Approx(1.0f).margin(0.01f));
	REQUIRE(halfRadius == Catch::Approx(0.5f).margin(0.01f));

	Test::destroyModule(m);
}