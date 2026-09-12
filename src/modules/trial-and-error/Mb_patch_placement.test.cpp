// Mb_patch_placement.test.cpp — reproduces a user report: after loading a real .vcv patch with
// a wide, sparse module layout (a Trench Crusade patch spanning ~200 HP, some modules at
// negative Y), many ModelPreviewWidgets ended up positioned outside PreviewWidget's own box.
//
// A minimal, self-contained repro rather than the real fixture: the plugin's own model (modelMb)
// is the only model in this test binary whose createModuleWidget() actually builds something
// (mock models via Mb.test.hpp's createMockModel() return NULL from the base
// plugin::Model::createModuleWidget(), which this preview path dereferences unconditionally),
// so every module entry below uses modelMb's own plugin/model slug. What matters for this bug is
// purely the *positions* (wide spread, some negative X/Y), not module diversity.
#include <set>
#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"
#include "Mb.cpp"
#include "Mb_patch_preview.hpp"
#include "Mb_patch_preview.cpp"

using namespace StoermelderPackOne;
using namespace StoermelderPackOne::Mb;
using namespace StoermelderPackOne::Mb::patch;

Test::TestContext<> testContext;

void testPluginInit(rack::Plugin* p) {
	pluginInstance = p;
	p->addModel(modelMb);
}

// Builds a patch JSON with `positions.size()` modules, each modelMb, at the given (HP, row)
// positions — matching the real .vcv's own coordinate convention (grid units, not pixels;
// PreviewWidget::createPreview() converts via RACK_GRID_SIZE).
static json_t* makePatchJson(const std::vector<std::pair<double, double>>& positions) {
	json_t* rootJ = json_object();
	json_t* modulesJ = json_array();
	int64_t id = 0;
	for (const auto& p : positions) {
		json_t* moduleJ = json_object();
		json_object_set_new(moduleJ, "id", json_integer(id++));
		json_object_set_new(moduleJ, "plugin", json_string("Stoermelder-P1"));
		json_object_set_new(moduleJ, "model", json_string("Mb"));
		json_t* posJ = json_array();
		json_array_append_new(posJ, json_real(p.first));
		json_array_append_new(posJ, json_real(p.second));
		json_object_set_new(moduleJ, "pos", posJ);
		json_array_append_new(modulesJ, moduleJ);
	}
	json_object_set_new(rootJ, "modules", modulesJ);
	json_object_set_new(rootJ, "cables", json_array());
	return rootJ;
}

// Like makePatchJson, but every position in `missingAt` is emitted as a reference to a
// plugin/model slug that isn't registered in this test binary (so createPreview() logs it as
// missing and skips creating a ModelPreviewWidget for it), while everything else uses modelMb.
static json_t* makePatchJsonWithMissing(const std::vector<std::pair<double, double>>& positions,
                                         const std::set<size_t>& missingAt) {
	json_t* rootJ = json_object();
	json_t* modulesJ = json_array();
	int64_t id = 0;
	for (size_t i = 0; i < positions.size(); i++) {
		const auto& p = positions[i];
		json_t* moduleJ = json_object();
		json_object_set_new(moduleJ, "id", json_integer(id++));
		bool missing = missingAt.count(i) > 0;
		json_object_set_new(moduleJ, "plugin", json_string(missing ? "SomeUnavailablePlugin" : "Stoermelder-P1"));
		json_object_set_new(moduleJ, "model", json_string(missing ? "SomeUnavailableModel" : "Mb"));
		json_t* posJ = json_array();
		json_array_append_new(posJ, json_real(p.first));
		json_array_append_new(posJ, json_real(p.second));
		json_object_set_new(moduleJ, "pos", posJ);
		json_array_append_new(modulesJ, moduleJ);
	}
	json_object_set_new(rootJ, "modules", modulesJ);
	json_object_set_new(rootJ, "cables", json_array());
	return rootJ;
}

// Every ModelPreviewWidget child's final box must lie within PreviewWidget's own box (allowing
// a tiny float-rounding tolerance, not a real placement slack) — the property fitPreviewToBox()
// exists to guarantee, and the one the reported bug violates by hundreds of pixels, not by
// float epsilon.
static bool allBoxesWithinBounds(PreviewWidget& preview) {
	const float eps = 0.01f;
	for (widget::Widget* child : preview.children) {
		ModelPreviewWidget* box = dynamic_cast<ModelPreviewWidget*>(child);
		if (!box) continue;
		Rect boxRect = box->box;
		Rect bounds = preview.box.zeroPos();
		if (boxRect.pos.x < bounds.pos.x - eps || boxRect.pos.y < bounds.pos.y - eps) return false;
		if (boxRect.pos.x + boxRect.size.x > bounds.size.x + eps) return false;
		if (boxRect.pos.y + boxRect.size.y > bounds.size.y + eps) return false;
	}
	return true;
}

TEST_CASE("A wide, sparse patch layout keeps every module box within the preview bounds", "[Mb][patch][preview][placement]") {
	PreviewWidget preview;
	preview.box.size = Vec(800, 600);

	// Mirrors the real Trench Crusade patch's shape: ~200 HP wide, a few rows tall, some
	// modules above row 0 (negative Y).
	json_t* rootJ = makePatchJson({
		{-168, 1},
		{-164, 0},
		{-74, 1},
		{29, -4},
		{34, 0},
	});

	REQUIRE(preview.setPatch("test", rootJ));
	// setPatch() only calls createPreview() + refreshPreview(); fitPreviewToBox() itself runs
	// from step(), gated on box.size/fileId having changed since the last call.
	preview.step();

	REQUIRE(preview.fitted);
	CHECK(allBoxesWithinBounds(preview));
}

// The same layout, but step()'d twice — reproducing any state that only breaks once
// fitPreviewToBox() has already run once (e.g. a value computed relative to a previous
// call's result instead of being rederived from an absolute source each time).
TEST_CASE("A second step() with the same box size does not move modules out of bounds", "[Mb][patch][preview][placement]") {
	PreviewWidget preview;
	preview.box.size = Vec(800, 600);

	json_t* rootJ = makePatchJson({
		{-168, 1},
		{-164, 0},
		{-74, 1},
		{29, -4},
		{34, 0},
	});

	REQUIRE(preview.setPatch("test", rootJ));
	preview.step();
	REQUIRE(preview.fitted);
	CHECK(allBoxesWithinBounds(preview));

	preview.step();
	CHECK(allBoxesWithinBounds(preview));
}

// A resize after the initial fit — the one path fitPreviewToBox() explicitly detects and
// reruns for (step()'s `box.size != lastBoxSize` check).
TEST_CASE("Resizing the preview after a fit keeps modules within the new bounds", "[Mb][patch][preview][placement]") {
	PreviewWidget preview;
	preview.box.size = Vec(800, 600);

	json_t* rootJ = makePatchJson({
		{-168, 1},
		{-164, 0},
		{-74, 1},
		{29, -4},
		{34, 0},
	});

	REQUIRE(preview.setPatch("test", rootJ));
	preview.step();
	REQUIRE(preview.fitted);
	CHECK(allBoxesWithinBounds(preview));

	preview.box.size = Vec(500, 400);
	preview.step();
	CHECK(allBoxesWithinBounds(preview));
}

// Reproduces a second regression found after the sizePreview() fix above: a box whose live
// preview subtree is lazily created (draw() -> createPreview()) resets modelBoxZoomApplied to
// force step() to reapply the current zoom to the newly-built subtree. But ModelPreviewWidget's
// step() recomputed box.pos as `originalPos.mult(modelBoxZoom)` with no centering offset, so the
// very next step() after a lazy creation snapped a correctly-centered box back to its unscaled,
// uncentered position — pushing it outside the preview bounds whenever the fit wasn't already an
// exact, uncentered fill (i.e. whenever contentWidth/contentHeight's aspect ratio didn't exactly
// match the box's). Fixed by storing the fit's centering offset on each box (contentOffset) and
// having both step() and sizePreview() add it back in whenever they recompute box.pos from
// originalPos, instead of only fitPreviewToBox() applying it once as a one-off.
//
// createPreview() itself touches APP->window (real GL setup) and can't run headlessly, so this
// simulates only the state mutation that actually matters for the bug: resetting
// modelBoxZoomApplied so the next step() re-derives box.pos, exactly as createPreview() does.
TEST_CASE("A box's lazily-created preview does not un-center it on the next step()", "[Mb][patch][preview][placement]") {
	PreviewWidget preview;
	// A box size whose aspect ratio doesn't match the content's, so fitting produces a non-zero
	// centering offset on at least one axis — the case the bug needs to be visible in.
	preview.box.size = Vec(800, 600);

	json_t* rootJ = makePatchJson({
		{-168, 1},
		{-164, 0},
		{-74, 1},
		{29, -4},
		{34, 0},
	});

	REQUIRE(preview.setPatch("test", rootJ));
	preview.step();
	REQUIRE(preview.fitted);
	CHECK(allBoxesWithinBounds(preview));

	bool anyNonZeroOffset = false;
	for (widget::Widget* child : preview.children) {
		ModelPreviewWidget* box = dynamic_cast<ModelPreviewWidget*>(child);
		if (!box) continue;
		if (std::abs(box->contentOffset.x) > 0.5f || std::abs(box->contentOffset.y) > 0.5f) {
			anyNonZeroOffset = true;
		}
		// Simulates draw() -> createPreview() being called on this box for the first time,
		// without the real (window-dependent) GL setup createPreview() also does.
		box->modelBoxZoomApplied = -1.f;
	}
	REQUIRE(anyNonZeroOffset);

	preview.step();
	CHECK(allBoxesWithinBounds(preview));
}

// Reproduces the real regression found via the "guitar-hero-3" .vcv (which has many modules from
// plugins not installed in the reporting user's setup): createPreview() normalizes originalPos
// against the *whole patch's* leftmost/topmost module position, including ones it then skips as
// missing — but fitPreviewToBox() re-derives its own content-bounds minimum from only the
// surviving children. When the true leftmost/topmost module is one of the missing ones, that
// re-derived minimum is not (0,0), and every surviving box's originalPos.mult(scale) is offset
// from where fitPreviewToBox() actually placed the content — pushing boxes out of bounds by
// exactly that (scaled) gap. Left column (index 0) and top row (index 1) are both missing here,
// mirroring the real patch (many "Model not found" warnings before the misplaced boxes appeared).
TEST_CASE("Missing models at the content's own top-left edge do not offset surviving boxes out of bounds", "[Mb][patch][preview][placement]") {
	PreviewWidget preview;
	preview.box.size = Vec(800, 600);

	std::vector<std::pair<double, double>> positions = {
		{0, 5},    // 0: missing — the true leftmost module
		{50, 0},   // 1: missing — the true topmost module
		{10, 1},
		{20, 2},
		{40, 3},
		{48, 4},
	};
	json_t* rootJ = makePatchJsonWithMissing(positions, {0, 1});

	REQUIRE(preview.setPatch("test", rootJ));
	preview.step();
	REQUIRE(preview.fitted);
	CHECK(allBoxesWithinBounds(preview));
}
