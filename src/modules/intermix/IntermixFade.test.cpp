#include "../../test/framework.hpp"

#include "IntermixFade.cpp"

using namespace StoermelderPackOne::Intermix;

SYNC_MODEL(modelIntermix, "Intermix");
SYNC_MODEL(modelIntermixFade, "IntermixFade");
Test::TestContext<> testContext;

// Mock that captures the actual float values passed to expSetFade.
// Used to verify that the expander sends seconds directly, not seconds * maxFade.
template<int PORTS>
struct CapturingIntermixMock : Module, IntermixBase<PORTS> {
	alignas(16) float currentMatrix[PORTS][PORTS];
	int channelCount = 1;
	uint32_t fadeInTs[PORTS] = {};
	uint32_t fadeOutTs[PORTS] = {};

	float lastFadeIn[PORTS] = {};
	float lastFadeOut[PORTS] = {};
	bool fadeInReceived = false;
	bool fadeOutReceived = false;

	CapturingIntermixMock() {
		config(0, 0, 0, 0);
		// Set model so expander check passes (isIntermixModel(), IntermixBase.hpp) — relies on
		// SYNC_MODEL(modelIntermix, "Intermix") above actually having landed; a missing/wrong
		// sync would make that check silently fail instead of erroring here.
		Test::requireModelSync(modelIntermix, "Intermix");
		model = modelIntermix;
		for (int i = 0; i < PORTS; i++)
			for (int j = 0; j < PORTS; j++)
				currentMatrix[i][j] = 0.f;
	}

	typename IntermixBase<PORTS>::IntermixMatrix expGetCurrentMatrix() override { return currentMatrix; }
	int expGetChannelCount() override { return channelCount; }

	void expSetFade(int i, float* fadeIn, float* fadeOut) override {
		if (fadeIn) {
			fadeInReceived = true;
			for (int j = 0; j < PORTS; j++) lastFadeIn[j] = fadeIn[j];
		}
		if (fadeOut) {
			fadeOutReceived = true;
			for (int j = 0; j < PORTS; j++) lastFadeOut[j] = fadeOut[j];
		}
	}

	void process(const ProcessArgs& args) override {
		rightExpander.producerMessage = (IntermixBase<PORTS>*)this;
		rightExpander.messageFlipRequested = true;
	}
};


// Forward declare Intermix module type for expander tests
template<int PORTS>
struct IntermixModuleMock : Module, IntermixBase<PORTS> {
	alignas(16) float currentMatrix[PORTS][PORTS];
	int channelCount = 1;
	uint32_t fadeInTs[PORTS] = {};
	uint32_t fadeOutTs[PORTS] = {};
	
	IntermixModuleMock() {
		config(0, 0, 0, 0);
		// Set model so expander check passes (isIntermixModel(), IntermixBase.hpp) — relies on
		// SYNC_MODEL(modelIntermix, "Intermix") above actually having landed; a missing/wrong
		// sync would make that check silently fail instead of erroring here.
		Test::requireModelSync(modelIntermix, "Intermix");
		model = modelIntermix;
		for (int i = 0; i < PORTS; i++) {
			for (int j = 0; j < PORTS; j++) {
				currentMatrix[i][j] = 0.f;
			}
		}
	}
	
	typename IntermixBase<PORTS>::IntermixMatrix expGetCurrentMatrix() override {
		return currentMatrix;
	}
	
	int expGetChannelCount() override { return channelCount; }
	
	void expSetFade(int i, float* fadeIn, float* fadeOut) override {
		if (fadeIn) fadeInTs[i]++;
		if (fadeOut) fadeOutTs[i]++;
	}
	
	void process(const ProcessArgs& args) override {
		rightExpander.producerMessage = (IntermixBase<PORTS>*)this;
		rightExpander.messageFlipRequested = true;
	}
};


TEST_CASE("Construction and initialization", "[IntermixFade]") {
	IntermixFadeModule<8>* m = Test::createModule<IntermixFadeModule<8>>("IntermixFade");
	IntermixFadeWidget* mw = Test::createWidget<IntermixFadeWidget>("IntermixFade");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);

	Test::destroyWidget(mw);
	Test::destroyModule(m);
}

TEST_CASE("Preset JSON null-guards", "[IntermixFade][JSON]") {
	auto module = Test::createModule<IntermixFadeModule<8>>("IntermixFade");

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

TEST_CASE("JSON round-trip preserves state", "[JSON][IntermixFade]") {
	IntermixFadeModule<8>* m = Test::createModule<IntermixFadeModule<8>>("IntermixFade");
	IntermixFadeModule<8>* m2 = Test::createModule<IntermixFadeModule<8>>("IntermixFade");

	m->panelTheme = 1;
	m->input = 4;
	m->fade = FADE::OUT;
	m->fadeLengthMode = FADE_LENGTH_60S;

	json_t* j = m->dataToJson();
	// Start m2 at a different value so dataFromJson() is genuinely exercised
	// (otherwise a fresh module's default could mask a broken restore).
	m2->panelTheme = 0;
	m2->input = 0;
	m2->fade = FADE::INOUT;
	m2->fadeLengthMode = FADE_LENGTH_15S;
	m2->dataFromJson(j);
	json_decref(j);

	REQUIRE(m2->panelTheme == 1);
	REQUIRE(m2->input == 4);
	REQUIRE(m2->fade == FADE::OUT);
	REQUIRE(m2->fadeLengthMode == FADE_LENGTH_60S);

	Test::destroyModule(m);
	Test::destroyModule(m2);
}


TEST_CASE("Reset behavior", "[IntermixFade]") {
	auto module = Test::createModule<IntermixFadeModule<8>>("IntermixFade");

	SECTION("Reset clears settings") {
		module->input = 5;
		module->fade = FADE::IN;
		
		rack::engine::Module::ResetEvent re;
		module->onReset(re);
		
		REQUIRE(module->input == 0);
		REQUIRE(module->fade == FADE::INOUT);
	}

	Test::destroyModule(module);
}

TEST_CASE("Input selection", "[IntermixFade]") {
	auto module = Test::createModule<IntermixFadeModule<8>>("IntermixFade");

	SECTION("Input can be changed") {
		module->input = 3;
		REQUIRE(module->input == 3);
		
		module->input = 7;
		REQUIRE(module->input == 7);
	}

	Test::destroyModule(module);
}

TEST_CASE("Fade mode", "[IntermixFade]") {
	auto module = Test::createModule<IntermixFadeModule<8>>("IntermixFade");

	SECTION("Fade mode can be changed") {
		module->fade = FADE::IN;
		REQUIRE(module->fade == FADE::IN);
		
		module->fade = FADE::OUT;
		REQUIRE(module->fade == FADE::OUT);
		
		module->fade = FADE::INOUT;
		REQUIRE(module->fade == FADE::INOUT);
	}

	Test::destroyModule(module);
}

TEST_CASE("Fade parameters", "[IntermixFade]") {
	auto module = Test::createModule<IntermixFadeModule<8>>("IntermixFade");

	SECTION("Fade time parameters can be set") {
		module->params[IntermixFadeModule<8>::PARAM_FADE + 0].setValue(2.5f);
		module->params[IntermixFadeModule<8>::PARAM_FADE + 1].setValue(5.0f);
		
		REQUIRE(module->params[IntermixFadeModule<8>::PARAM_FADE + 0].getValue() == 2.5f);
		REQUIRE(module->params[IntermixFadeModule<8>::PARAM_FADE + 1].getValue() == 5.0f);
	}

	SECTION("Fade time range is 0-15 seconds") {
		module->params[IntermixFadeModule<8>::PARAM_FADE + 0].setValue(0.f);
		REQUIRE(module->params[IntermixFadeModule<8>::PARAM_FADE + 0].getValue() == 0.f);
		
		module->params[IntermixFadeModule<8>::PARAM_FADE + 0].setValue(15.f);
		REQUIRE(module->params[IntermixFadeModule<8>::PARAM_FADE + 0].getValue() == 15.f);
	}

	Test::destroyModule(module);
}

TEST_CASE("Expander connection", "[IntermixFade]") {
	auto fadeModule = Test::createModule<IntermixFadeModule<8>>("IntermixFade");

	SECTION("Module processes without expander") {
		// Should not crash
		fadeModule->process(Test::makeProcessArgs(1));
	}

	Test::destroyModule(fadeModule);
}

TEST_CASE("Fade control via expander", "[IntermixFade]") {
	auto intermixModule = new IntermixModuleMock<8>();
	auto fadeModule = Test::createModule<IntermixFadeModule<8>>("IntermixFade");

	SECTION("Fade IN mode sends fade in times") {
		intermixModule->rightExpander.module = fadeModule;
		fadeModule->leftExpander.module = intermixModule;
		
		fadeModule->fade = FADE::IN;
		fadeModule->input = 0;
		fadeModule->params[IntermixFadeModule<8>::PARAM_FADE + 0].setValue(2.0f);
		fadeModule->params[IntermixFadeModule<8>::PARAM_FADE + 1].setValue(3.0f);
		
		intermixModule->channelCount = 1;
		
		uint32_t oldTs = intermixModule->fadeInTs[0];
		
		// Initial process to set up producer message
		intermixModule->process(Test::makeProcessArgs(1));
		intermixModule->rightExpander.consumerMessage = intermixModule->rightExpander.producerMessage;
		
		// Process several times to trigger divider
		for (int i = 0; i < 100; i++) {
			intermixModule->process(Test::makeProcessArgs(1));
			// Manually flip producer to consumer message (simulates engine behavior)
			intermixModule->rightExpander.consumerMessage = intermixModule->rightExpander.producerMessage;
			fadeModule->process(Test::makeProcessArgs(1));
		}
		
		// Check that fade in timestamp was updated
		REQUIRE(intermixModule->fadeInTs[0] > oldTs);
	}

	SECTION("Fade OUT mode sends fade out times") {
		intermixModule->rightExpander.module = fadeModule;
		fadeModule->leftExpander.module = intermixModule;
		
		fadeModule->fade = FADE::OUT;
		fadeModule->input = 0;
		fadeModule->params[IntermixFadeModule<8>::PARAM_FADE + 0].setValue(1.5f);
		
		intermixModule->channelCount = 1;
		
		uint32_t oldTs = intermixModule->fadeOutTs[0];
		
		// Initial process to set up producer message
		intermixModule->process(Test::makeProcessArgs(1));
		intermixModule->rightExpander.consumerMessage = intermixModule->rightExpander.producerMessage;
		
		for (int i = 0; i < 100; i++) {
			intermixModule->process(Test::makeProcessArgs(1));
			// Manually flip producer to consumer message (simulates engine behavior)
			intermixModule->rightExpander.consumerMessage = intermixModule->rightExpander.producerMessage;
			fadeModule->process(Test::makeProcessArgs(1));
		}
		
		REQUIRE(intermixModule->fadeOutTs[0] > oldTs);
	}

	SECTION("Fade INOUT mode sends both times") {
		intermixModule->rightExpander.module = fadeModule;
		fadeModule->leftExpander.module = intermixModule;
		
		fadeModule->fade = FADE::INOUT;
		fadeModule->input = 0;
		
		intermixModule->channelCount = 1;
		
		uint32_t oldTsIn = intermixModule->fadeInTs[0];
		uint32_t oldTsOut = intermixModule->fadeOutTs[0];
		
		// Initial process to set up producer message
		intermixModule->process(Test::makeProcessArgs(1));
		intermixModule->rightExpander.consumerMessage = intermixModule->rightExpander.producerMessage;
		
		for (int i = 0; i < 100; i++) {
			intermixModule->process(Test::makeProcessArgs(1));
			// Manually flip producer to consumer message (simulates engine behavior)
			intermixModule->rightExpander.consumerMessage = intermixModule->rightExpander.producerMessage;
			fadeModule->process(Test::makeProcessArgs(1));
		}
		
		REQUIRE(intermixModule->fadeInTs[0] > oldTsIn);
		REQUIRE(intermixModule->fadeOutTs[0] > oldTsOut);
	}

	Test::destroyModule(fadeModule);
	delete intermixModule;
}

TEST_CASE("Different inputs with fade", "[IntermixFade]") {
	auto intermixModule = new IntermixModuleMock<8>();
	auto fadeModule = Test::createModule<IntermixFadeModule<8>>("IntermixFade");

	SECTION("Changing input affects different matrix row") {
		intermixModule->rightExpander.module = fadeModule;
		fadeModule->leftExpander.module = intermixModule;
		
		intermixModule->channelCount = 1;
		
		// Initial process to set up producer message
		intermixModule->process(Test::makeProcessArgs(1));
		intermixModule->rightExpander.consumerMessage = intermixModule->rightExpander.producerMessage;
		
		// Test input 0
		fadeModule->input = 0;
		fadeModule->fade = FADE::INOUT;
		uint32_t oldTs0 = intermixModule->fadeInTs[0];
		
		for (int i = 0; i < 100; i++) {
			intermixModule->process(Test::makeProcessArgs(1));
			// Manually flip producer to consumer message (simulates engine behavior)
			intermixModule->rightExpander.consumerMessage = intermixModule->rightExpander.producerMessage;
			fadeModule->process(Test::makeProcessArgs(1));
		}
		
		REQUIRE(intermixModule->fadeInTs[0] > oldTs0);
		
		// Test input 1
		fadeModule->input = 1;
		uint32_t oldTs1 = intermixModule->fadeInTs[1];
		
		for (int i = 0; i < 100; i++) {
			intermixModule->process(Test::makeProcessArgs(1));
			// Manually flip producer to consumer message (simulates engine behavior)
			intermixModule->rightExpander.consumerMessage = intermixModule->rightExpander.producerMessage;
			fadeModule->process(Test::makeProcessArgs(1));
		}
		
		REQUIRE(intermixModule->fadeInTs[1] > oldTs1);
	}

	Test::destroyModule(fadeModule);
	delete intermixModule;
}

TEST_CASE("Expander chain", "[IntermixFade]") {
	auto intermixModule = new IntermixModuleMock<8>();
	auto fadeModule1 = Test::createModule<IntermixFadeModule<8>>("IntermixFade");
	auto fadeModule2 = Test::createModule<IntermixFadeModule<8>>("IntermixFade");
	Test::SimpleEngine engine;
	engine.registerModules(intermixModule, fadeModule1, fadeModule2);

	SECTION("Multiple expanders can chain") {
		// Setup expander chain: Intermix -> Fade1 -> Fade2
		intermixModule->rightExpander.module = fadeModule1;
		fadeModule1->leftExpander.module = intermixModule;
		fadeModule1->rightExpander.module = fadeModule2;
		fadeModule2->leftExpander.module = fadeModule1;

		intermixModule->channelCount = 1;

		fadeModule1->input = 0;
		fadeModule2->input = 1;
		fadeModule1->fade = FADE::IN;
		fadeModule2->fade = FADE::IN;

		// Process many times to trigger divider (64 samples division)
		for (int i = 0; i < 130; i++) {
			engine.step();
		}

		// Verify fade1 sent fade in times to intermix
		uint32_t fade1InTs = intermixModule->fadeInTs[0];
		REQUIRE(fade1InTs > 0);

		// Verify fade2 sent fade in times to fade1 (which should have forwarded)
		uint32_t fade2InTs = intermixModule->fadeInTs[1];
		REQUIRE(fade2InTs > 0);
	}

	Test::destroyModule(fadeModule2);
	Test::destroyModule(fadeModule1);
	delete intermixModule;
}


TEST_CASE("FadeParamQuantity max value follows fadeLengthMode", "[IntermixFade][fade-time]") {
	auto module = Test::createModule<IntermixFadeModule<8>>("IntermixFade");

	SECTION("FADE_LENGTH_4S gives max 4s") {
		module->fadeLengthMode = FADE_LENGTH_4S;
		auto* pq = module->paramQuantities[IntermixFadeModule<8>::PARAM_FADE + 0];
		REQUIRE(pq->getMaxValue() == Catch::Approx(4.0f).margin(0.001f));
	}

	SECTION("FADE_LENGTH_15S gives max 15s") {
		module->fadeLengthMode = FADE_LENGTH_15S;
		auto* pq = module->paramQuantities[IntermixFadeModule<8>::PARAM_FADE + 0];
		REQUIRE(pq->getMaxValue() == Catch::Approx(15.0f).margin(0.001f));
	}

	SECTION("FADE_LENGTH_60S gives max 60s") {
		module->fadeLengthMode = FADE_LENGTH_60S;
		auto* pq = module->paramQuantities[IntermixFadeModule<8>::PARAM_FADE + 0];
		REQUIRE(pq->getMaxValue() == Catch::Approx(60.0f).margin(0.001f));
	}

	Test::destroyModule(module);
}


TEST_CASE("Expander fade time: param value sent to expSetFade as seconds", "[IntermixFade][fade-time]") {
	// FadeParamQuantity::getMaxValue() dynamically scales the knob to [0, maxFade],
	// so getValue() already returns seconds. The expander must NOT multiply by maxFade
	// again: v[i] = getValue() * maxFade would make a 5s knob send 75s in 15s mode.

	SECTION("15s mode (default): 5s knob position sends 5s") {
		auto* mock = new CapturingIntermixMock<8>();
		auto* fade = Test::createModule<IntermixFadeModule<8>>("IntermixFade");

		mock->rightExpander.module = fade;
		fade->leftExpander.module = mock;

		fade->fadeLengthMode = FADE_LENGTH_15S;
		fade->fade = FADE::IN;
		fade->input = 0;
		for (int j = 0; j < 8; j++)
			fade->params[IntermixFadeModule<8>::PARAM_FADE + j].setValue(5.0f);

		// The expander's sceneDivider fires every 64 calls; 100 steps guarantees it.
		Test::SimpleEngine engine;
		engine.registerModules(mock, fade);
		for (int i = 0; i < 100; i++) engine.step();

		REQUIRE(mock->fadeInReceived);
		// Bug: receives 5.0 * 15 = 75.0. Correct: receives 5.0.
		REQUIRE(mock->lastFadeIn[0] == Catch::Approx(5.0f).margin(0.001f));

		Test::destroyModule(fade);
		delete mock;
	}

	SECTION("4s mode: 2s knob position sends 2s") {
		auto* mock = new CapturingIntermixMock<8>();
		auto* fade = Test::createModule<IntermixFadeModule<8>>("IntermixFade");

		mock->rightExpander.module = fade;
		fade->leftExpander.module = mock;

		fade->fadeLengthMode = FADE_LENGTH_4S;
		fade->fade = FADE::IN;
		fade->input = 0;
		for (int j = 0; j < 8; j++)
			fade->params[IntermixFadeModule<8>::PARAM_FADE + j].setValue(2.0f);

		Test::SimpleEngine engine;
		engine.registerModules(mock, fade);
		for (int i = 0; i < 100; i++) engine.step();

		REQUIRE(mock->fadeInReceived);
		// Bug: receives 2.0 * 4 = 8.0. Correct: receives 2.0.
		REQUIRE(mock->lastFadeIn[0] == Catch::Approx(2.0f).margin(0.001f));

		Test::destroyModule(fade);
		delete mock;
	}

	SECTION("60s mode: 10s knob position sends 10s") {
		auto* mock = new CapturingIntermixMock<8>();
		auto* fade = Test::createModule<IntermixFadeModule<8>>("IntermixFade");

		mock->rightExpander.module = fade;
		fade->leftExpander.module = mock;

		fade->fadeLengthMode = FADE_LENGTH_60S;
		fade->fade = FADE::OUT;
		fade->input = 0;
		for (int j = 0; j < 8; j++)
			fade->params[IntermixFadeModule<8>::PARAM_FADE + j].setValue(10.0f);

		Test::SimpleEngine engine;
		engine.registerModules(mock, fade);
		for (int i = 0; i < 100; i++) engine.step();

		REQUIRE(mock->fadeOutReceived);
		// Bug: receives 10.0 * 60 = 600.0. Correct: receives 10.0.
		REQUIRE(mock->lastFadeOut[0] == Catch::Approx(10.0f).margin(0.001f));

		Test::destroyModule(fade);
		delete mock;
	}
}
