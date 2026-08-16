#include "../../plugin.hpp"
#include "AhabRenderer.hpp"
#include "Ahab.hpp"
#include "AhabEditorState.hpp"
#include <cmath>
#include <nanovg.h>

namespace StoermelderPackOne {
namespace Ahab {

// Glyph classification helper returning AhabRenderer::GlyphClass
static AhabRenderer::GlyphClass glyph_class_of(char glyph) {
	if (glyph == '.')
		return AhabRenderer::Glyph_grid;
	if (glyph >= '0' && glyph <= '9')
		return AhabRenderer::Glyph_numeric;
	switch (glyph) {
		case 'N':
		case 'E':
		case 'S':
		case 'W':
			return AhabRenderer::Glyph_movement;
		case '!':
		case ':':
		case ';':
		case '=':
		case '%':
		case '?':
		case '>':
		case '<':
			return AhabRenderer::Glyph_uppercase; // punctuation-like treated as lowercase
		case '*':
			return AhabRenderer::Glyph_bang;
		case '#':
			return AhabRenderer::Glyph_comment;
	}
	if (glyph >= 'A' && glyph <= 'Z')
		return AhabRenderer::Glyph_uppercase;
	if (glyph >= 'a' && glyph <= 'z')
		return AhabRenderer::Glyph_lowercase;
	return AhabRenderer::Glyph_unknown;
}

// Helper to map simple terminal colors (approximate)
static NVGcolor __attribute__((unused)) color_from_name(int name, float alpha = 1.0f) {
	switch (name) {
	case 0: // natural
		return nvgRGBAf(1, 1, 1, alpha);
	case 1: // black
		return nvgRGBAf(0.07f, 0.07f, 0.07f, alpha);
	case 2: // red
		return nvgRGBAf(0.83f, 0.23f, 0.19f, alpha);
	case 3: // green
		return nvgRGBAf(0.20f, 0.8f, 0.2f, alpha);
	case 4: // yellow
		return nvgRGBAf(0.95f, 0.8f, 0.2f, alpha);
	case 5: // blue
		return nvgRGBAf(0.2f, 0.6f, 0.95f, alpha);
	case 6: // magenta
		return nvgRGBAf(0.8f, 0.2f, 0.75f, alpha);
	case 7: // cyan
		return nvgRGBAf(0.14f, 0.86f, 0.88f, alpha);
	case 8: // white
		return nvgRGBAf(1, 1, 1, alpha);
	default:
		return nvgRGBAf(1, 1, 1, alpha);
	}
}

AhabRenderer::AhabRenderer() {}

void AhabRenderer::setFontSize(float size) {
	fontSize = size;
}

// Compute cell sizing for a given Field within an area size
void AhabRenderer::computeCellAndPadding(const math::Vec& areaSize, const Field* f, float& cell_w_out, float& cell_h_out) {
	float w = (float)(f && f->width ? f->width : 1);
	float h = (float)(f && f->height ? f->height : 1);
	float area_w = areaSize.x - 2 * this->pad;
	float area_h = areaSize.y - 2 * this->pad;
	float cell_w = area_w / w;
	float cell_h = area_h / h;
	cell_w_out = cell_w;
	cell_h_out = cell_h;
}

// Convert a pixel position (local to widget) to cell coordinates inside given areaSize.
bool AhabRenderer::pixelToCell(const math::Vec& pos, const math::Vec& areaSize, const Field* f, Usz& y, Usz& x) {
	float cell_w, cell_h;
	computeCellAndPadding(areaSize, f, cell_w, cell_h);
	if (cell_w <= 0 || cell_h <= 0) return false;
	if (pos.x < this->pad || pos.y < this->pad) return false;
	if (pos.x >= areaSize.x - this->pad || pos.y >= areaSize.y - this->pad) return false;
	// Event positions are already local to the Widget, so subtract only the padding
	float rel_x = pos.x - this->pad - 0.5f;
	float rel_y = pos.y - this->pad - 0.5f;
	int cx = (int)std::floor(rel_x / cell_w);
	int cy = (int)std::floor(rel_y / cell_h);
	x = (Usz)cx; y = (Usz)cy;
	return true;
}

void AhabRenderer::draw(NVGcontext* vg, const Field* field, const Mark* mbuf, const math::Vec& areaSize, const AhabEditorState& editorState, bool isPlaying) {
	if (!vg || !field || !field->buffer)
		return;
	// Compute cell sizes based on areaSize and field
	float cell_w, cell_h;
	computeCellAndPadding(areaSize, field, cell_w, cell_h);
	// Choose font size so glyphPaddingRatio == 1.0 fills a cell exactly (no extra border).
	// For the chosen monospace font, a glyph's width is approximately fontSize * 0.5,
	// so we pick fontSize = glyphPaddingRatio * min(cell_h * 2.0f, cell_w * 2.0f).
	float chosenFontSize = this->glyphPaddingRatio * std::min(cell_h * 2.0f, cell_w * 2.0f);
	setFontSize(chosenFontSize);
	// Use renderer's pad as origin for drawing
	float x = this->pad;
	float y = this->pad;
	Usz h = field->height;
	Usz w = field->width;

	// Setup text properties (draw centered in each cell)
	nvgFontSize(vg, fontSize);
	auto fontFace_ = APP->window->loadFont(asset::plugin(pluginInstance, "res/fonts/VictorMono-SemiBold.ttf"));
	nvgFontFaceId(vg, fontFace_->handle);
	nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);

	// Determine highlight target: if cursor is on a lowercase variable, highlight that char
	char highlightChar = '\0';
	if (editorState.cursor_x < w && editorState.cursor_y < h) {
		char cur = field->buffer[editorState.cursor_y * w + editorState.cursor_x];
		if ((cur >= 'a' && cur <= 'z') || (cur >= '0' && cur <= '9')) highlightChar = cur;
	}

	for (Usz ry = 0; ry < h; ++ry) {
		for (Usz rx = 0; rx < w; ++rx) {
			char g = field->buffer[ry * w + rx];
			Mark_flags mf = mbuf ? mbuffer_peek((Mark *)mbuf, h, w, ry, rx) :
						Mark_flag_none;

			// Determine foreground/background colors (use configurable members)
			NVGcolor fg = fgDefault;
			NVGcolor bg = color::BLACK_TRANSPARENT;

			AhabRenderer::GlyphClass gclass = glyph_class_of(g);
			switch (gclass) {
				case AhabRenderer::Glyph_unknown:
					fg = color::RED;
					bg = color::BLACK_TRANSPARENT;
					break;
				case AhabRenderer::Glyph_grid:
					fg = fgGrid;
					break;
				case AhabRenderer::Glyph_comment:
					fg = fgComment;
					break;
				case AhabRenderer::Glyph_uppercase:
					// Uppercase glyphs are currently not universally inverted; keep default fg
					fg = color::BLACK;
					bg = bgUppercase;
					break;
				case AhabRenderer::Glyph_movement:
					fg = bgUppercase;
					break;
				case AhabRenderer::Glyph_lowercase:
				case AhabRenderer::Glyph_numeric:
					fg = fgSleep;
					break;
				case AhabRenderer::Glyph_bang:
					fg = fgDefault;
					break;
			}

			if (gclass != AhabRenderer::Glyph_comment) {
				if ((mf & (Mark_flag_lock | Mark_flag_input)) == (Mark_flag_lock | Mark_flag_input)) {
					// Standard locking input
					fg = fgDefault;
					bg = color::BLACK_TRANSPARENT;
				}
				else if ((mf & Mark_flag_input) == Mark_flag_input) {
					// Non-locking input
					fg = fgDefault;
					bg = color::BLACK_TRANSPARENT;
				} 
				else if (mf & (Mark_flag_lock)) {
					// Locked only
					fg = fgLock;
					bg = color::BLACK_TRANSPARENT;
				}
			}	
			if (mf & Mark_flag_output) {
				// A_reverse in orca-c: swap fg/bg colors
				// For terminal reverse video effect: if bg was transparent, use fg color as new bg
				// and use black (or dark) as new fg for contrast
				if (bg.a < 0.5f) {
					// bg was transparent, so use the original fg as the new bg
					bg = fgDefault;
					fg = nvgRGBAf(0.0f, 0.0f, 0.0f, 1.0f); // black text on colored bg
				} 
				else {
					// Both were solid colors, just swap them
					std::swap(fg, bg);
				}
			}
			if (mf & Mark_flag_haste_input) {
				fg = bgHasteInput;
				bg = color::BLACK_TRANSPARENT;
			}
			if (mf & Mark_flag_uninit && gclass != AhabRenderer::Glyph_grid) {
				// not yet initialized: draw as comment
				fg = fgComment;
				bg = color::BLACK_TRANSPARENT;
			}

			float px = x + rx * cell_w;
			float py = y + ry * cell_h;
			// Draw background if set (or to create grid cell)
			if (bg.a > 0.001f) {
				nvgBeginPath(vg);
				nvgRect(vg, px, py, cell_w, cell_h);
				nvgFillColor(vg, bg);
				nvgFill(vg);
			}

			int step_col = this->gridStepCol > 0 ? this->gridStepCol : 8;
			int step_row = this->gridStepRow > 0 ? this->gridStepRow : 8;
			char tmpChar[2] = {g, '\0'};
			const char* textToDraw = tmpChar;
			if (g == '.') {
				// Draw cursor overlay (if within bounds)
				if (rx == editorState.cursor_x && ry == editorState.cursor_y) {
					// If insert mode is active, draw a white frame around the cursor cell
					if (editorState.getInsertMode()) {
						// Slight inset so the stroke doesn't cut off on edges
						nvgBeginPath(vg);
						nvgRect(vg, px - 0.3f, py - 0.3f, cell_w + 0.6f, cell_h + 0.6f);
						nvgStrokeColor(vg, nvgRGBAf(1.0f, 1.0f, 1.0f, 0.65f));
						nvgStrokeWidth(vg, 1.f);
						nvgStroke(vg);
					}
					tmpChar[0] = isPlaying ? '@' : '~';
					nvgFillColor(vg, fgDefault);
				}
				else {
					// Only draw intersection marker when both horizontal and vertical steps coincide
					if ((step_col > 0 && step_row > 0) && ((rx % (Usz)step_col == 0) && (ry % (Usz)step_row == 0))) {
						// intersection marker
						tmpChar[0] = '+';
						nvgFillColor(vg, intersectionColor);
					}
					else {
						// use middle dot U+00B7 (UTF-8 0xC2 0xB7)
						textToDraw = "\xC2\xB7";
						nvgFillColor(vg, fg);
					}
				}
			}
			else {
				// non-empty glyph
				nvgFillColor(vg, fg);
			}
			// draw centered in the cell
			float tx = px + cell_w * 0.5f;
			float ty = py + cell_h * 0.5f + this->glyphYOffset;
			
			// If this glyph matches the highlighted char, draw it with highlight font color
			// Skip highlighting for comment cells and cells marked as locked or sleeping
			if (highlightChar != '\0' && g == highlightChar && g != '.' && g != '#' && !(mf & (Mark_flag_lock | Mark_flag_sleep))) {
				nvgFillColor(vg, varHighlightFontColor);
			}
			nvgText(vg, tx, ty, textToDraw, NULL);
		}
	}

	// Draw selection overlay (selection is always active)
	// clamp selection to viewport
	Usz sy = editorState.sel_y, sx = editorState.sel_x, sh = editorState.sel_h, sw = editorState.sel_w;
	// Ensure selection within bounds
	if (sy < h && sx < w) {
		Usz vis_h = sh;
		Usz vis_w = sw;
		if (sy + vis_h > h) vis_h = h - sy;
		if (sx + vis_w > w) vis_w = w - sx;
		if (vis_h > 0 && vis_w > 0) {
			float sx_px = x + (float)sx * cell_w;
			float sy_px = y + (float)sy * cell_h;
			float sw_px = (float)vis_w * cell_w;
			float sh_px = (float)vis_h * cell_h;
			nvgBeginPath(vg);
			nvgRect(vg, sx_px, sy_px, sw_px, sh_px);
			nvgFillColor(vg, selectionFillColor);
			nvgFill(vg);
			nvgStrokeColor(vg, selectionStrokeColor);
			nvgStrokeWidth(vg, 1.5f);
			nvgStroke(vg);
		}
	}
}

} // namespace Ahab
} // namespace StoermelderPackOne