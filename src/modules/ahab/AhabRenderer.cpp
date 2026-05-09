#include "../../plugin.hpp"
#include "AhabRenderer.hpp"
#include "Ahab.hpp"
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
static NVGcolor color_from_name(int name, float alpha = 1.0f) {
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

void AhabRenderer::setCursor(Usz y, Usz x) {
	cursor_y = std::max(sel_y, std::min(y, sel_y + sel_h - 1));
	cursor_x = std::max(sel_x, std::min(x, sel_x + sel_w - 1));
}

void AhabRenderer::getCursor(Usz& y, Usz& x) const {
	y = cursor_y;
	x = cursor_x;
}

void AhabRenderer::moveCursorRelative(int dy, int dx, Usz field_h, Usz field_w, bool extendSelection) {
	// Clamp relative move to grid bounds
	if (field_h == 0 || field_w == 0) return;
	int ny = (int)cursor_y + dy;
	int nx = (int)cursor_x + dx;
	if (ny < 0) ny = 0;
	if (nx < 0) nx = 0;
	if ((Usz)ny >= field_h) ny = (int)field_h - 1;
	if ((Usz)nx >= field_w) nx = (int)field_w - 1;
	cursor_y = (Usz)ny;
	cursor_x = (Usz)nx;
	if (extendSelection) updateSelectionToCursor();
}

// Selection methods
void AhabRenderer::toggleSelectionAtCursor() {
	// Selection is always active — toggling starts a new 1x1 selection at cursor
	sel_anchor_y = cursor_y;
	sel_anchor_x = cursor_x;
	sel_y = cursor_y; sel_x = cursor_x; sel_h = 1; sel_w = 1;
}

void AhabRenderer::clearSelection() {
	// Selection cannot be disabled; collapse to a 1x1 selection at the cursor
	sel_anchor_y = cursor_y;
	sel_anchor_x = cursor_x;
	sel_y = cursor_y; sel_x = cursor_x; sel_h = 1; sel_w = 1;
}

void AhabRenderer::updateSelectionToCursor() {
	sel_y = std::min(sel_anchor_y, cursor_y);
	sel_x = std::min(sel_anchor_x, cursor_x);
	sel_h = std::max(sel_anchor_y, cursor_y) - sel_y + 1;
	sel_w = std::max(sel_anchor_x, cursor_x) - sel_x + 1;
}

void AhabRenderer::setSelection(Usz y, Usz x, Usz h, Usz w, Usz field_h, Usz field_w) {
	// Prevent zero-size selection
	if (h == 0) h = 1;
	if (w == 0) w = 1;
	// If no bounds provided, behave as before
	if (field_h == (Usz)std::numeric_limits<Usz>::max() || field_w == (Usz)std::numeric_limits<Usz>::max()) {
		sel_y = y; sel_x = x; sel_h = h; sel_w = w;
		sel_anchor_y = y; sel_anchor_x = x;
		return;
	}
	// If field has zero dimension, clear selection
	if (field_h == 0 || field_w == 0) {
		clearSelection();
		return;
	}
	// Clamp height/vertical start
	if (h > field_h) {
		y = 0;
		h = field_h;
	} else if (y > field_h - h) {
		y = field_h - h;
	}
	// Clamp width/horizontal start
	if (w > field_w) {
		x = 0;
		w = field_w;
	} else if (x > field_w - w) {
		x = field_w - w;
	}
	sel_y = y; sel_x = x; sel_h = h; sel_w = w;
	sel_anchor_y = y; sel_anchor_x = x;
	setCursor(cursor_y, cursor_x); // clamp cursor to new selection
}

void AhabRenderer::getSelectionRect(Usz& y, Usz& x, Usz& h, Usz& w) const {
	y = sel_y; x = sel_x; h = sel_h; w = sel_w;
}

void AhabRenderer::draw(NVGcontext* vg, const Field* field, const Mark* mbuf, const math::Vec& areaSize, bool isPlaying) {
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
	if (cursor_x < w && cursor_y < h) {
		char cur = field->buffer[cursor_y * w + cursor_x];
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
				if (rx == cursor_x && ry == cursor_y) {
					// If insert mode is active, draw a white frame around the cursor cell
					if (getInsertMode()) {
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
	Usz sy = sel_y, sx = sel_x, sh = sel_h, sw = sel_w;
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