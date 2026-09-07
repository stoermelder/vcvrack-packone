#pragma once
#include "../../plugin.hpp"
#include <orca-c/field.h>
#include <orca-c/gbuffer.h>
#include <nanovg.h>
#include "AhabEditorState.hpp"

namespace StoermelderPackOne {
namespace Ahab {

struct AhabRenderer {
	float fontSize = 12.0f;
	// Padding (px) used around the field drawing area
	float pad = 6.0f;
	// Ratio between cell height and glyph font size (0..1)
	float glyphPaddingRatio = 0.75f;

	// Vertical offset (px) to apply to glyphs measured from the top of each cell.
	// Positive values move glyphs downward. Default 0.0.
	float glyphYOffset = -0.6f;

	// Grid ruler step size (cells) used for drawing sparse intersection markers.
	// `gridStepCol`: horizontal step (columns). `gridStepRow`: vertical step (rows).
	// These can be configured per-module via the context menu and are stored in module JSON.
	int gridStepCol = 8;
	int gridStepRow = 8;

	// Glyph classification (mirror from orca-c)
	enum GlyphClass {
		Glyph_unknown,
		Glyph_grid,
		Glyph_comment,
		Glyph_uppercase,
		Glyph_lowercase,
		Glyph_movement,
		Glyph_numeric,
		Glyph_bang,
	};

	AhabRenderer();

	void setFontSize(float size);
	float getFontSize() const { return fontSize; }

	// Compute cell sizing for a given Field within an area size
	void computeCellAndPadding(const math::Vec& areaSize, const Field* f, float& cell_w_out, float& cell_h_out);
	// Draw a field within the given areaSize. Padding and font are chosen by the renderer.
	// `editorState` supplies the cursor / selection / insert-mode editing state.
	// `isPlaying` controls whether cursor-over-empty shows '@' (playing) or '~' (stopped)
	void draw(NVGcontext* vg, const Field* field, const Mark* mbuf, const math::Vec& areaSize, const AhabEditorState& editorState, bool isPlaying = false);

	// Cursor fill color (used to draw a filled rectangle under the cursor)
	// Darkened green with transparency by default
	NVGcolor cursorColor = nvgRGBAf(0.06f, 0.44f, 0.20f, 0.55f);

	// Palette colors used by the renderer (adjustable as public members)
	NVGcolor fgDefault = nvgRGBAf(1, 1, 1, 1);
	NVGcolor fgComment = nvgRGBAf(0.6f, 0.6f, 0.6f, 1.0f);
	NVGcolor bgUppercase = nvgRGBAf(0.06f, 0.66f, 0.66f, 1.0f);
	NVGcolor fgGrid = nvgRGBAf(0.15f, 0.15f, 0.15f, 1.0f);
	NVGcolor bgOutput = nvgRGBAf(0.2f, 0.5f, 0.85f, 1.0f);
	NVGcolor bgHasteInput = nvgRGBAf(0.06f, 0.66f, 0.66f, 1.0f);
	NVGcolor fgLock = nvgRGBAf(0.45f, 0.45f, 0.45f, 1.0f);
	NVGcolor fgSleep = nvgRGBAf(0.4f, 0.4f, 0.4f, 1.0f);
	NVGcolor intersectionColor = nvgRGBAf(0.3f, 0.3f, 0.3f, 1.f);
	NVGcolor selectionFillColor = nvgRGBAf(1.f, 0.7f, 0.27f, 0.3f);
	NVGcolor selectionStrokeColor = nvgRGBAf(1.0f, 1.0f, 1.0f, 0.15f);

	// Highlight color for variable occurrences when cursor is over a lowercase variable (box)
	NVGcolor varHighlightColor = nvgRGBAf(0.14f, 0.86f, 0.88f, 0.22f);
	// Highlight font color to use instead of default when highlighting variables
	NVGcolor varHighlightFontColor = nvgRGBAf(0.95f, 0.8f, 0.2f, 1.0f);

	// Convert a pixel position (local to widget) to cell coordinates within a given area.
	bool pixelToCell(const math::Vec& pos, const math::Vec& areaSize, const Field* f, Usz& y, Usz& x);
};

} // namespace Ahab
} // namespace StoermelderPackOne