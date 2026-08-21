#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"
#include "../../test/test_mock.hpp"
#include "PanicRoom.cpp"

using namespace StoermelderPackOne;
using namespace StoermelderPackOne::PanicRoom;

SYNC_MODEL(modelPanicRoom, "PanicRoom");
static Test::TestContext<> testContext;

// A ModuleAccess mock that records removeModule calls and returns scripted widgets.
struct MockModuleAccess : vcv::ModuleAccess {
	mutable int getModuleWidgetsCalls = 0;
	std::vector<ModuleWidget*> widgets;
	std::vector<int64_t> removedIds;

	std::vector<ModuleWidget*> getModuleWidgets() const override {
		getModuleWidgetsCalls++;
		return widgets;
	}

	void removeModule(int64_t moduleId) override {
		removedIds.push_back(moduleId);
	}
};

// A CableAccess mock that records removeCable calls and returns scripted cables.
struct MockCableAccess : vcv::CableAccess {
	std::vector<CableWidget*> cables;
	std::vector<CableWidget*> removed;

	const std::vector<CableWidget*> getCompleteCables() const override {
		return cables;
	}

	void removeCable(CableWidget* cw, bool addToHistory) override {
		removed.push_back(cw);
	}
};

// A non-null parent so the module-limit loop doesn't break on the first widget.
static rack::widget::Widget dummyParent;

// Builds a fake ModuleWidget with a fake engine::Module of the given id.
static rack::app::ModuleWidget* makeFakeWidget(int64_t id) {
	rack::app::ModuleWidget* mw = new rack::app::ModuleWidget;
	rack::engine::Module* m = new rack::engine::Module;
	m->id = id;
	mw->module = m;
	mw->parent = &dummyParent;
	return mw;
}

// Detaches and frees a fake widget created by makeFakeWidget().
static void destroyFakeWidget(rack::app::ModuleWidget* mw) {
	rack::engine::Module* m = mw->module;
	mw->module = NULL;  // ~ModuleWidget would otherwise remove it from the engine
	mw->parent = NULL;  // ~Widget asserts !parent
	delete mw;
	delete m;
}

// Builds a fake CableWidget (parent set so the limit loop doesn't break on it).
static rack::app::CableWidget* makeFakeCable() {
	rack::app::CableWidget* cw = new rack::app::CableWidget;
	cw->parent = &dummyParent;
	return cw;
}

// Frees a fake cable created by makeFakeCable().
static void destroyFakeCable(rack::app::CableWidget* cw) {
	cw->parent = NULL;  // ~Widget asserts !parent
	delete cw;
}


TEST_CASE("PanicRoom module limit reads modules through the module access", "[PanicRoom][module]") {
	auto mock = Test::makeMockVcv<MockModuleAccess>();
	auto module = Test::createModule<PanicRoomModule>("PanicRoom");
	auto widget = Test::createWidget<PanicRoomWidget>(module);

	SECTION("No removal when at the limit") {
		rack::app::ModuleWidget* mw1 = makeFakeWidget(1);
		rack::app::ModuleWidget* mw2 = makeFakeWidget(2);
		rack::app::ModuleWidget* mw3 = makeFakeWidget(3);
		mock.modules.widgets = {mw1, mw2, mw3};

		module->moduleLimitEnabled = true;
		module->moduleLimit = 3;

		int callsBefore = mock.modules.getModuleWidgetsCalls;
		widget->step();

		// step() consults getModuleWidgets() through the mock, but nothing is removed.
		REQUIRE(mock.modules.getModuleWidgetsCalls > callsBefore);
		CHECK(mock.modules.removedIds.empty());

		destroyFakeWidget(mw1);
		destroyFakeWidget(mw2);
		destroyFakeWidget(mw3);
	}

	Test::destroyWidget(widget);
	Test::destroyModule(module);
}

TEST_CASE("PanicRoom module limit removes excess modules through the module access", "[PanicRoom][module]") {
	auto mock = Test::makeMockVcv<MockModuleAccess>();
	auto module = Test::createModule<PanicRoomModule>("PanicRoom");
	auto widget = Test::createWidget<PanicRoomWidget>(module);

	SECTION("Removes the most recently added modules down to the limit") {
		rack::app::ModuleWidget* mw1 = makeFakeWidget(1);
		rack::app::ModuleWidget* mw2 = makeFakeWidget(2);
		rack::app::ModuleWidget* mw3 = makeFakeWidget(3);
		mock.modules.widgets = {mw1, mw2, mw3};

		module->moduleLimitEnabled = true;
		module->moduleLimit = 3;
		widget->step();  // 3 <= 3 → enforcement arms, nothing removed

		module->moduleLimit = 1;
		widget->step();  // 3 > 1 → remove the two most recently added (ids 3, 2)

		REQUIRE(mock.modules.removedIds.size() == 2);
		CHECK(mock.modules.removedIds[0] == 3);
		CHECK(mock.modules.removedIds[1] == 2);

		destroyFakeWidget(mw1);
		destroyFakeWidget(mw2);
		destroyFakeWidget(mw3);
	}

	Test::destroyWidget(widget);
	Test::destroyModule(module);
}

TEST_CASE("PanicRoom cable limit removes excess cables through the cable access", "[PanicRoom][cable]") {
	auto mock = Test::makeMockVcv<MockCableAccess>();
	auto module = Test::createModule<PanicRoomModule>("PanicRoom");
	auto widget = Test::createWidget<PanicRoomWidget>(module);

	SECTION("Removes the most recently added cables down to the limit") {
		rack::app::CableWidget* cw1 = makeFakeCable();
		rack::app::CableWidget* cw2 = makeFakeCable();
		rack::app::CableWidget* cw3 = makeFakeCable();
		mock.cables.cables = {cw1, cw2, cw3};

		module->cableLimitEnabled = true;
		module->cableLimit = 3;
		widget->step();  // 3 <= 3 → enforcement arms, nothing removed

		module->cableLimit = 1;
		widget->step();  // 3 > 1 → remove the two most recently added (cw3, cw2)

		REQUIRE(mock.cables.removed.size() == 2);
		CHECK(mock.cables.removed[0] == cw3);
		CHECK(mock.cables.removed[1] == cw2);

		destroyFakeCable(cw1);
		destroyFakeCable(cw2);
		destroyFakeCable(cw3);
	}

	Test::destroyWidget(widget);
	Test::destroyModule(module);
}