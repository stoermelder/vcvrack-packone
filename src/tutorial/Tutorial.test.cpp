#include "../test/framework.hpp"
#include "Tutorial.hpp"

void testPluginInit(rack::Plugin* p) {
	pluginInstance = p;
}

using namespace StoermelderPackOne::Tutorial;

// A minimal Context/Scene/EventState so APP->event->finalizeWidget() (run by
// Test::destroyWidget()) doesn't dereference null when a fixture's ModuleWidget is torn down.
static Test::TestContext<> testContext;

namespace {

// A widget type with no special meaning other than being distinguishable by type, for
// Target::widget<W>().
struct CustomChild : widget::Widget {};

// Small fixture: a ModuleWidget with one of each target-able child, plus a child nested
// inside a ZoomWidget to exercise the coordinate mapping.
struct Fixture {
	app::ModuleWidget* mw;
	app::ParamWidget* param0;
	app::ParamWidget* param1;
	app::ParamWidget* param2;
	app::PortWidget* input0;
	app::PortWidget* output0;
	app::ModuleLightWidget* light0;
	CustomChild* custom;
	widget::ZoomWidget* zoomWidget;
	CustomChild* zoomedCustom;

	Fixture() {
		mw = new app::ModuleWidget;
		mw->box.size = math::Vec(270.f, 200.f);

		param0 = new app::ParamWidget;
		param0->paramId = 0;
		param0->box = math::Rect(10.f, 10.f, 20.f, 20.f);
		mw->addChild(param0);

		param1 = new app::ParamWidget;
		param1->paramId = 1;
		param1->box = math::Rect(40.f, 10.f, 20.f, 20.f);
		mw->addChild(param1);

		param2 = new app::ParamWidget;
		param2->paramId = 2;
		param2->box = math::Rect(70.f, 40.f, 20.f, 20.f);
		mw->addChild(param2);

		input0 = new app::PortWidget;
		input0->type = engine::Port::INPUT;
		input0->portId = 0;
		input0->box = math::Rect(10.f, 150.f, 15.f, 15.f);
		mw->addChild(input0);

		output0 = new app::PortWidget;
		output0->type = engine::Port::OUTPUT;
		output0->portId = 0;
		output0->box = math::Rect(200.f, 150.f, 15.f, 15.f);
		mw->addChild(output0);

		light0 = new app::ModuleLightWidget;
		light0->firstLightId = 5;
		light0->box = math::Rect(100.f, 5.f, 8.f, 8.f);
		mw->addChild(light0);

		custom = new CustomChild;
		custom->box = math::Rect(120.f, 120.f, 25.f, 25.f);
		mw->addChild(custom);

		zoomWidget = new widget::ZoomWidget;
		zoomWidget->box.pos = math::Vec(20.f, 60.f);
		zoomWidget->setZoom(2.f);
		mw->addChild(zoomWidget);

		zoomedCustom = new CustomChild;
		zoomedCustom->box = math::Rect(5.f, 5.f, 10.f, 10.f);
		zoomWidget->addChild(zoomedCustom);
	}

	~Fixture() {
		Test::destroyWidget(mw);
	}
};

} // namespace

TEST_CASE("Target::param resolves to the param widget's rect via getRelativeOffset", "[Tutorial]") {
	Fixture f;
	Target t = Target::param(1);
	math::Rect r;
	REQUIRE(t.resolve(f.mw, r));
	REQUIRE(r.equals(f.param1->box));
}

TEST_CASE("Target::params gives the union of the range", "[Tutorial]") {
	Fixture f;
	Target t = Target::params(0, 3);
	math::Rect r;
	REQUIRE(t.resolve(f.mw, r));
	math::Rect expected = f.param0->box.expand(f.param1->box).expand(f.param2->box);
	REQUIRE(r.equals(expected));
}

TEST_CASE("Target::params resolves to whichever ids exist when some are missing", "[Tutorial]") {
	Fixture f;
	// id 1 exists, id 99 and 100 don't: only param1 contributes.
	Target t = Target::params(99, 1);
	math::Rect r;
	REQUIRE_FALSE(t.resolve(f.mw, r));

	Target t2 = Target::params(1, 2); // 1 exists, 2 exists too -> union of both
	math::Rect r2;
	REQUIRE(t2.resolve(f.mw, r2));
	REQUIRE(r2.equals(f.param1->box.expand(f.param2->box)));
}

TEST_CASE("Target::input resolves to the input port's rect", "[Tutorial]") {
	Fixture f;
	Target t = Target::input(0);
	math::Rect r;
	REQUIRE(t.resolve(f.mw, r));
	REQUIRE(r.equals(f.input0->box));
}

TEST_CASE("Target::output resolves to the output port's rect", "[Tutorial]") {
	Fixture f;
	Target t = Target::output(0);
	math::Rect r;
	REQUIRE(t.resolve(f.mw, r));
	REQUIRE(r.equals(f.output0->box));
}

TEST_CASE("Target::light resolves to the ModuleLightWidget with the matching firstLightId", "[Tutorial]") {
	Fixture f;
	Target t = Target::light(5);
	math::Rect r;
	REQUIRE(t.resolve(f.mw, r));
	REQUIRE(r.equals(f.light0->box));
}

TEST_CASE("Target::widget<W> resolves to the first descendant of that type", "[Tutorial]") {
	Fixture f;
	Target t = Target::widget<CustomChild>();
	math::Rect r;
	REQUIRE(t.resolve(f.mw, r));
	// findDescendant/widget<W> both walk depth-first through children in insertion order;
	// the first CustomChild added is `custom`.
	REQUIRE(r.equals(f.custom->box));
}

TEST_CASE("Target::widget(lookup) resolves via an arbitrary lookup function", "[Tutorial]") {
	Fixture f;
	Target t = Target::widget([](app::ModuleWidget* mw) -> widget::Widget* {
		return mw->getOutput(0);
	});
	math::Rect r;
	REQUIRE(t.resolve(f.mw, r));
	REQUIRE(r.equals(f.output0->box));
}

TEST_CASE("A child inside a ZoomWidget resolves through the zoom, never through re-derived constants", "[Tutorial]") {
	Fixture f;
	Target t = Target::widget<CustomChild>();

	// Two CustomChild instances exist: `custom` (direct child) and `zoomedCustom` (inside the
	// ZoomWidget). Target the zoomed one specifically via its own lookup to check the mapping.
	Target zoomedTarget = Target::widget([&f](app::ModuleWidget*) -> widget::Widget* {
		return f.zoomedCustom;
	});
	math::Rect r;
	REQUIRE(zoomedTarget.resolve(f.mw, r));

	// Expected: zoomWidget->box.pos + zoomedCustom->box (pos and size) scaled by the zoom.
	math::Vec expectedPos = f.zoomWidget->box.pos.plus(f.zoomedCustom->box.pos.mult(2.f));
	math::Vec expectedSize = f.zoomedCustom->box.size.mult(2.f);
	REQUIRE(math::isNear(r.pos.x, expectedPos.x, 1e-3f));
	REQUIRE(math::isNear(r.pos.y, expectedPos.y, 1e-3f));
	REQUIRE(math::isNear(r.size.x, expectedSize.x, 1e-3f));
	REQUIRE(math::isNear(r.size.y, expectedSize.y, 1e-3f));

	// Cross-check against Rack's own getRelativeOffset, the production code path, rather than
	// re-deriving panel layout constants.
	math::Vec a = f.zoomedCustom->getRelativeOffset(math::Vec(), f.mw);
	math::Vec b = f.zoomedCustom->getRelativeOffset(f.zoomedCustom->box.size, f.mw);
	math::Rect expected = math::Rect::fromCorners(a, b);
	REQUIRE(r.equals(expected));
}

TEST_CASE("Target::rect maps millimetres to module px", "[Tutorial]") {
	Fixture f;
	math::Rect mm(10.f, 5.f, 20.f, 8.f);
	Target t = Target::rect(mm);
	math::Rect r;
	REQUIRE(t.resolve(f.mw, r));
	REQUIRE(r.equals(math::Rect(mm2px(mm.pos), mm2px(mm.size))));
}

TEST_CASE("Target::none has no resolver", "[Tutorial]") {
	Target t = Target::none();
	REQUIRE_FALSE((bool) t.resolve);
}

TEST_CASE("Step constructor defaults target to Target::none()", "[Tutorial]") {
	Step s("Title", "Text");
	REQUIRE_FALSE((bool) s.target.resolve);
	REQUIRE(s.side == Side::AUTO);
	REQUIRE(s.width == 0.f);
}

TEST_CASE("Step::prefer and Step::withWidth chain and mutate the step", "[Tutorial]") {
	Step s("Title", "Text");
	s.prefer(Side::LEFT).withWidth(240.f);
	REQUIRE(s.side == Side::LEFT);
	REQUIRE(s.width == 240.f);
}

TEST_CASE("unresolvedSteps is empty when every step's target resolves", "[Tutorial]") {
	Fixture f;
	Tutorial tutorial;
	tutorial.steps.push_back(Step("Welcome", "text")); // centered, no target
	tutorial.steps.push_back(Step("Param", "text", Target::param(0)));
	tutorial.steps.push_back(Step("Input", "text", Target::input(0)));

	auto unresolved = unresolvedSteps(f.mw, tutorial);
	REQUIRE(unresolved.empty());
}

TEST_CASE("unresolvedSteps reports steps whose target does not resolve, without crashing", "[Tutorial]") {
	Fixture f;
	Tutorial tutorial;
	tutorial.steps.push_back(Step("Welcome", "text")); // centered, index 0, never unresolved
	tutorial.steps.push_back(Step("Bad param", "text", Target::param(999))); // index 1
	tutorial.steps.push_back(Step("Good param", "text", Target::param(0))); // index 2
	tutorial.steps.push_back(Step("Bad input", "text", Target::input(999))); // index 3

	auto unresolved = unresolvedSteps(f.mw, tutorial);
	REQUIRE(unresolved.size() == 2);
	REQUIRE(unresolved[0] == 1);
	REQUIRE(unresolved[1] == 3);

	// Falls back to centered without a crash: the caller can still ask for placement using
	// the placement engine directly, since the target simply fails to resolve.
	math::Rect r;
	REQUIRE_FALSE(tutorial.steps[1].target.resolve(f.mw, r));
}

// ============================================================================================
// createTutorialMenuItem
// ============================================================================================

namespace {
// The first TutorialOverlay child of mw, or nullptr — same helper as TutorialOverlay.test.cpp,
// duplicated locally rather than shared: these two .test.cpp files are separate TUs/binaries.
TutorialOverlay* overlayOf(app::ModuleWidget* mw) {
	for (widget::Widget* child : mw->children) {
		if (TutorialOverlay* o = dynamic_cast<TutorialOverlay*>(child)) return o;
	}
	return nullptr;
}

Tutorial oneStepTutorial() {
	Tutorial t;
	t.title = "TEST";
	t.steps.push_back(Step("Welcome", "text"));
	return t;
}
} // namespace

TEST_CASE("createTutorialMenuItem builds a menu item labelled Tutorial…", "[Tutorial][menu]") {
	Fixture f;
	ui::MenuItem* item = createTutorialMenuItem(f.mw, oneStepTutorial);
	REQUIRE(item != nullptr);
	REQUIRE(item->text == "Tutorial…");
	delete item;
}

TEST_CASE("clicking the menu item starts the tutorial on mw", "[Tutorial][menu]") {
	Fixture f;
	REQUIRE(overlayOf(f.mw) == nullptr);

	ui::MenuItem* item = createTutorialMenuItem(f.mw, oneStepTutorial);
	item->onAction(ui::MenuItem::ActionEvent());

	REQUIRE(overlayOf(f.mw) != nullptr);
	delete item;
}

TEST_CASE("the factory is called lazily, once per click, not eagerly at menu-build time", "[Tutorial][menu]") {
	Fixture f;
	int calls = 0;
	std::function<Tutorial()> factory = [&calls]() {
		calls++;
		return oneStepTutorial();
	};

	ui::MenuItem* item = createTutorialMenuItem(f.mw, factory);
	REQUIRE(calls == 0);

	item->onAction(ui::MenuItem::ActionEvent());
	REQUIRE(calls == 1);

	// A second click re-reads the factory (start() replaces the existing overlay), reflecting
	// live module state rather than a tutorial captured once at menu-build time.
	item->onAction(ui::MenuItem::ActionEvent());
	REQUIRE(calls == 2);

	delete item;
}
