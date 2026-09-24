#pragma once

// Shared preamble for the PANICROOM test suite.
// Included by PanicRoom.test.cpp, which pulls the test cases in from PanicRoom.test.module.hpp
// inside their own namespace.

#include "../../test/framework.hpp"
#include "PanicRoom.cpp"

using namespace StoermelderPackOne;
using namespace StoermelderPackOne::PanicRoom;

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

struct ModuleMock {
	TEST_MOCK_MODULES(MockModuleAccess);
};

struct CableMock {
	TEST_MOCK_CABLES(MockCableAccess);
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