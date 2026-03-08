#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"

#include "IntermixFade.cpp"

using namespace StoermelderPackOne::Intermix;

// Forward declare Intermix module type for expander tests
template<int PORTS>
struct IntermixModuleMock : Module, IntermixBase<PORTS> {
	alignas(16) float currentMatrix[PORTS][PORTS];
	int channelCount = 1;
	uint32_t fadeInTs[PORTS] = {};
	uint32_t fadeOutTs[PORTS] = {};
	
	IntermixModuleMock() {
		config(0, 0, 0, 0);
		// Set model so expander check passes
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

Test::TestContext<> testContext;


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

TEST_CASE("JSON serialization", "[JSON][IntermixFade]") {
	auto module = Test::createModule<IntermixFadeModule<8>>("IntermixFade");

	SECTION("Module state is serialized and deserialized") {
		module->panelTheme = 1;
		module->input = 4;
		module->fade = FADE::OUT;
		
		json_t* rootJ = module->dataToJson();
		REQUIRE(rootJ != nullptr);
		
		auto moduleNew = Test::createModule<IntermixFadeModule<8>>("IntermixFade");
		moduleNew->dataFromJson(rootJ);
		
		REQUIRE(moduleNew->panelTheme == 1);
		REQUIRE(moduleNew->input == 4);
		REQUIRE(moduleNew->fade == FADE::OUT);
		
		json_decref(rootJ);
		Test::destroyModule(moduleNew);
	}

	Test::destroyModule(module);
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
