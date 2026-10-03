#pragma once
#include "../plugin.hpp"
#include "../vcv/ui.hpp"
#include "../vcv/fs.hpp"
#include "ScriptEditorText.hpp"
#include <ui/ScrollWidget.hpp>
#include <algorithm>
#include <climits>
#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// A basic overlay text editor for scripts, in the manner of AutoTagDialog: a
// fixed-size dialog centered in a modal ui::MenuOverlay. It edits a copy of the
// text; the owner supplies an apply callback and decides what applying means.
// The dialog knows nothing about MIDI-KIT.
// Rack's ui::TextField is single-line (see ScriptEditField), so the editing
// widget here brings its own multi-line drawing, hit testing and caret movement.

namespace StoermelderPackOne {
namespace ui {
namespace editor {

using namespace rack;

struct ScriptEditorDialog;

// ScriptEditField
// ui::TextField honours `multiline` only for Enter: it renders through one
// bndTextField() call, has no line-aware hit testing and its Up/Down handling
// is column-less. This subclass keeps the base class's text and
// cursor/selection model (insertText, clipboard, selectAll) and replaces
// drawing, hit testing and vertical/line-wise caret movement.
// Geometry is fixed and monospace (VictorMono advances 0.6 em), so a column is
// a multiple of charWidth and hit testing needs no font metrics. There is no
// wrapping and no horizontal scroll: a line longer than the box is clipped.

struct ScriptEditField : TextField {
	static constexpr float kFontSize = 13.f;
	static constexpr float kLineHeight = 16.f;
	// Cell width. The nominal guess (0.6 em) is replaced by the font's real advance the
	// first time anything is drawn; it is shared by the field and the gutter. Headless
	// (no window, no draw) the guess stays, which is all hit testing there needs.
	static float& charWidth() {
		static float w = 0.6f * kFontSize;
		return w;
	}

	static void measureCharWidth(NVGcontext* vg) {
		static const char* probe = "MMMMMMMMMMMMMMMM";
		NVGglyphPosition pos[16];
		if (nvgTextGlyphPositions(vg, 0.f, 0.f, probe, NULL, pos, 16) == 16) {
			float w = (pos[15].x - pos[0].x) / 15.f;
			if (w > 1.f) charWidth() = w;
		}
	}
	static constexpr float kPadX = 6.f;
	static constexpr float kPadY = 4.f;
	static constexpr int kTabWidth = 4;

	// Ctrl+Enter / Ctrl+Shift+Enter (closeAfter) and Esc.
	std::function<void(bool closeAfter)> applyAction;
	std::function<void()> closeAction;
	std::function<void()> changeAction;
	// Ctrl+F opens the find bar, F3 / Shift+F3 jump to the next / previous match.
	std::function<void()> findAction;
	std::function<void(bool forward)> findNextAction;

	// ── Find ── what to highlight. The match list is rebuilt lazily after the text or
	// the search changes.
	std::string findNeedle;
	bool findMatchCase = false;
	std::vector<int> matches;
	bool matchesStale = true;

	void setFind(const std::string& needle, bool matchCase) {
		findNeedle = needle;
		findMatchCase = matchCase;
		matchesStale = true;
	}

	const std::vector<int>& getMatches() {
		if (matchesStale) {
			matches = scripttext::findAll(text, findNeedle, findMatchCase);
			matchesStale = false;
		}
		return matches;
	}

	// Selects match `index` (cursor at its end, so the caret follows it into view).
	void selectMatch(int index) {
		const std::vector<int>& m = getMatches();
		if (index < 0 || index >= (int)m.size()) return;
		selection = m[index];
		cursor = m[index] + (int)findNeedle.size();
		preferredCol = -1;
	}

	// Whether the current selection is exactly one of the matches.
	int currentMatchIndex() {
		const std::vector<int>& m = getMatches();
		int b = std::min(cursor, selection);
		int e = std::max(cursor, selection);
		int i = scripttext::firstMatchAtOrAfter(m, b);
		return i >= 0 && m[i] == b && e - b == (int)findNeedle.size() ? i : -1;
	}

	// Up/Down keep the column across short lines. `stickyCursor` is where the last
	// vertical move left the cursor; if the cursor is elsewhere the memory is stale.
	int preferredCol = -1;
	int stickyCursor = -1;
	// For keeping the caret in view: the state the scroll was last adjusted for.
	int lastCursor = -1;
	size_t lastTextSize = std::string::npos;

	ScriptEditField() {
		multiline = true;
	}

	static float contentHeight(int lines) {
		return lines * kLineHeight + 2.f * kPadY;
	}

	float colToX(int col) const {
		return kPadX + col * charWidth();
	}

	// Rect of the caret's line in this widget's own coordinates.
	Rect caretLineRect() const {
		scripttext::LineCol lc = scripttext::offsetToLineCol(text, cursor);
		return Rect(Vec(0.f, kPadY + lc.line * kLineHeight), Vec(box.size.x, kLineHeight));
	}

	int getTextPosition(Vec mousePos) override {
		int line = (int)std::floor((mousePos.y - kPadY) / kLineHeight);
		int col = (int)std::floor((mousePos.x - kPadX) / charWidth() + 0.5f);
		return scripttext::lineColToOffset(text, line, col);
	}

	// ── Undo / redo ──
	//
	// The editor's own, entirely separate from Rack's patch history: whole-buffer
	// snapshots, discarded with the editor. `current` is the state as of the last
	// change or cursor sync, i.e. what a change is about to replace.
	struct Snapshot {
		std::string text;
		int cursor = 0;
		int selection = 0;
	};
	static constexpr size_t kMaxUndo = 200;
	std::vector<Snapshot> undoStack;
	std::vector<Snapshot> redoStack;
	Snapshot current;
	bool restoring = false;
	// Typing coalesces into one undo step per word: a run of typed characters that
	// continues where the previous one ended. A space starts a new step.
	bool typingNow = false;
	bool typedBreak = false;
	bool lastWasTyping = false;
	int lastTypeEnd = -1;

	Snapshot snapshotNow() const {
		Snapshot s;
		s.text = text;
		s.cursor = cursor;
		s.selection = selection;
		return s;
	}

	// Moves and selections are not changes, so the pre-change cursor has to be taken
	// before the event that may change the text is handled.
	void syncCursor() {
		current.cursor = cursor;
		current.selection = selection;
	}

	void clearHistory() {
		undoStack.clear();
		redoStack.clear();
		current = snapshotNow();
		lastWasTyping = false;
		lastTypeEnd = -1;
	}

	void recordChange() {
		bool coalesce = typingNow && lastWasTyping && !typedBreak && !undoStack.empty()
			&& current.cursor == current.selection && current.cursor == lastTypeEnd;
		if (!coalesce) {
			undoStack.push_back(current);
			if (undoStack.size() > kMaxUndo) undoStack.erase(undoStack.begin());
		}
		redoStack.clear();
		lastWasTyping = typingNow;
		lastTypeEnd = cursor;
		current = snapshotNow();
	}

	void restore(const Snapshot& s) {
		bool changed = text != s.text;
		restoring = true;
		text = s.text;
		cursor = s.cursor;
		selection = s.selection;
		if (changed) {
			ChangeEvent eChange;
			onChange(eChange);
		}
		restoring = false;
		current = s;
		lastWasTyping = false;
		lastTypeEnd = -1;
		preferredCol = -1;
	}

	void undo() {
		if (undoStack.empty()) return;
		redoStack.push_back(snapshotNow());
		Snapshot s = undoStack.back();
		undoStack.pop_back();
		restore(s);
	}

	void redo() {
		if (redoStack.empty()) return;
		undoStack.push_back(snapshotNow());
		Snapshot s = redoStack.back();
		redoStack.pop_back();
		restore(s);
	}

	void onChange(const ChangeEvent& e) override {
		TextField::onChange(e);
		matchesStale = true;
		if (!restoring) recordChange();
		if (changeAction) changeAction();
	}

	void onSelectText(const SelectTextEvent& e) override {
		syncCursor();
		typingNow = true;
		typedBreak = e.codepoint == ' ';
		TextField::onSelectText(e);
		typingNow = false;
	}

	// Double-click selects the word, triple-click the line; a fourth click starts over.
	// Counted here rather than from Rack's double-click event, which has no third step.
	static constexpr double kMultiClickSeconds = 0.4;
	static constexpr float kMultiClickDistance = 5.f;
	double lastClickTime = -1e9;
	Vec lastClickPos;
	int clickCount = 0;
	// The word or line a double / triple click selected; dragging grows it from there.
	scripttext::Range multiClickAnchor;

	void onButton(const ButtonEvent& e) override {
		TextField::onButton(e);
		preferredCol = -1;
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			const double window = kMultiClickSeconds;
			const float reach = kMultiClickDistance;
			double now = vcv::fs::getTime();
			bool again = now - lastClickTime <= window && e.pos.minus(lastClickPos).norm() <= reach;
			clickCount = again ? clickCount % 3 + 1 : 1;
			lastClickTime = now;
			lastClickPos = e.pos;
			if (clickCount >= 2) {
				// Anchored at the start so a drag that follows extends from there.
				scripttext::Range r = clickCount == 2 ? scripttext::wordRangeAt(text, cursor) : scripttext::lineRangeAt(text, cursor);
				selection = r.begin;
				cursor = r.end;
				multiClickAnchor = r;
			}
		}
	}

	// Rack sends hover events while the button is held. For a plain press the base class
	// moves the caret to the pointer; after a double / triple click that would shrink the
	// selection to "word start .. pointer", so it extends by whole words / lines instead.
	void onDragHover(const DragHoverEvent& e) override {
		if (e.origin != this || clickCount < 2) {
			TextField::onDragHover(e);
			return;
		}
		OpaqueWidget::onDragHover(e);
		int pos = getTextPosition(e.pos);
		scripttext::Range r = clickCount == 2 ? scripttext::wordRangeAt(text, pos) : scripttext::lineRangeAt(text, pos);
		if (r.begin < multiClickAnchor.begin) {
			selection = multiClickAnchor.end;
			cursor = r.begin;
		}
		else {
			selection = multiClickAnchor.begin;
			cursor = std::max(r.end, multiClickAnchor.end);
		}
	}

	void step() override {
		TextField::step();
		// Follow the caret after any move or edit, but leave a wheel-scrolled view alone.
		if (cursor != lastCursor || text.size() != lastTextSize) {
			lastCursor = cursor;
			lastTextSize = text.size();
			if (ScrollWidget* scroll = getAncestorOfType<ScrollWidget>()) {
				scroll->scrollTo(caretLineRect());
			}
		}
	}

	void applyEdit(const scripttext::Edit& edit) {
		if (edit.text == text && edit.cursor == cursor && edit.selection == selection) return;
		bool changed = edit.text != text;
		text = edit.text;
		cursor = edit.cursor;
		selection = edit.selection;
		if (changed) {
			ChangeEvent eChange;
			onChange(eChange);
		}
	}

	void moveVertically(int delta, bool extendSelection) {
		if (cursor != stickyCursor) preferredCol = -1;
		if (preferredCol < 0) preferredCol = scripttext::offsetToLineCol(text, cursor).col;
		cursor = scripttext::moveVertical(text, cursor, delta, preferredCol);
		stickyCursor = cursor;
		if (!extendSelection) selection = cursor;
	}

	void moveToLineStart(bool extendSelection) {
		scripttext::LineCol lc = scripttext::offsetToLineCol(text, cursor);
		cursor = scripttext::lineColToOffset(text, lc.line, 0);
		if (!extendSelection) selection = cursor;
	}

	void moveToLineEnd(bool extendSelection) {
		scripttext::LineCol lc = scripttext::offsetToLineCol(text, cursor);
		cursor = scripttext::lineColToOffset(text, lc.line, INT_MAX);
		if (!extendSelection) selection = cursor;
	}

	void onSelectKey(const SelectKeyEvent& e) override {
		syncCursor();
		if (e.action == GLFW_PRESS || e.action == GLFW_REPEAT) {
			// Apply. Checked before the base class, which would insert a newline.
			if (e.key == GLFW_KEY_ENTER || e.key == GLFW_KEY_KP_ENTER) {
				if (e.isKeyCommand(e.key, RACK_MOD_CTRL) || e.isKeyCommand(e.key, RACK_MOD_CTRL | GLFW_MOD_SHIFT)) {
					if (e.action == GLFW_PRESS && applyAction) applyAction((e.mods & GLFW_MOD_SHIFT) != 0);
					e.consume(this);
					return;
				}
			}
			if (e.isKeyCommand(GLFW_KEY_F, RACK_MOD_CTRL)) {
				if (e.action == GLFW_PRESS && findAction) findAction();
				e.consume(this);
				return;
			}
			if (e.isKeyCommand(GLFW_KEY_F3) || e.isKeyCommand(GLFW_KEY_F3, GLFW_MOD_SHIFT)) {
				if (findNextAction) findNextAction((e.mods & GLFW_MOD_SHIFT) == 0);
				e.consume(this);
				return;
			}
			if (e.isKeyCommand(GLFW_KEY_ESCAPE)) {
				if (e.action == GLFW_PRESS && closeAction) closeAction();
				e.consume(this);
				return;
			}
			// Undo/redo on the editor's own history; Rack's patch history never sees these.
			if (e.isKeyCommand(GLFW_KEY_Z, RACK_MOD_CTRL)) {
				undo();
				e.consume(this);
				return;
			}
			if (e.isKeyCommand(GLFW_KEY_Z, RACK_MOD_CTRL | GLFW_MOD_SHIFT) || e.isKeyCommand(GLFW_KEY_Y, RACK_MOD_CTRL)) {
				redo();
				e.consume(this);
				return;
			}
			// Enter keeps the indentation of the line it splits.
			if (e.isKeyCommand(GLFW_KEY_ENTER) || e.isKeyCommand(GLFW_KEY_KP_ENTER)) {
				insertText("\n" + scripttext::leadingWhitespace(text, std::min(cursor, selection)));
				e.consume(this);
				return;
			}
			// Tab indents, Shift+Tab outdents; with a multi-line selection, every line it
			// touches. The base class would move focus to nextField.
			if (e.isKeyCommand(GLFW_KEY_TAB)) {
				bool multiLine = cursor != selection && scripttext::offsetToLineCol(text, cursor).line != scripttext::offsetToLineCol(text, selection).line;
				if (multiLine) {
					applyEdit(scripttext::indentLines(text, cursor, selection, kTabWidth));
				}
				else {
					insertText(std::string(kTabWidth, ' '));
				}
				e.consume(this);
				return;
			}
			if (e.isKeyCommand(GLFW_KEY_TAB, GLFW_MOD_SHIFT)) {
				applyEdit(scripttext::outdentLines(text, cursor, selection, kTabWidth));
				e.consume(this);
				return;
			}
			// Line operations. Each is one undo step.
			if (e.isKeyCommand(GLFW_KEY_D, RACK_MOD_CTRL)) {
				applyEdit(scripttext::duplicateLines(text, cursor, selection));
				e.consume(this);
				return;
			}
			if (e.isKeyCommand(GLFW_KEY_K, RACK_MOD_CTRL | GLFW_MOD_SHIFT)) {
				applyEdit(scripttext::deleteLines(text, cursor, selection));
				e.consume(this);
				return;
			}
			if (e.isKeyCommand(GLFW_KEY_UP, GLFW_MOD_ALT) || e.isKeyCommand(GLFW_KEY_DOWN, GLFW_MOD_ALT)) {
				applyEdit(scripttext::moveLines(text, cursor, selection, e.key == GLFW_KEY_UP ? -1 : 1));
				e.consume(this);
				return;
			}
			// Toggle comment: Ctrl+/, and Ctrl+Shift+7 for layouts that type "/" with Shift+7.
			if (e.isKeyCommand(GLFW_KEY_SLASH, RACK_MOD_CTRL) || e.isKeyCommand(GLFW_KEY_7, RACK_MOD_CTRL | GLFW_MOD_SHIFT)) {
				applyEdit(scripttext::toggleComment(text, cursor, selection, scripttext::commentPrefix(text)));
				e.consume(this);
				return;
			}
			if (e.isKeyCommand(GLFW_KEY_UP) || e.isKeyCommand(GLFW_KEY_UP, GLFW_MOD_SHIFT)) {
				moveVertically(-1, (e.mods & GLFW_MOD_SHIFT) != 0);
				e.consume(this);
				return;
			}
			if (e.isKeyCommand(GLFW_KEY_DOWN) || e.isKeyCommand(GLFW_KEY_DOWN, GLFW_MOD_SHIFT)) {
				moveVertically(1, (e.mods & GLFW_MOD_SHIFT) != 0);
				e.consume(this);
				return;
			}
			// The base class's Home/End scan for '\n' from the wrong side of the
			// cursor, which misplaces the cursor on an empty first line.
			for (int shift : {0, (int)GLFW_MOD_SHIFT}) {
				if (e.isKeyCommand(GLFW_KEY_HOME, shift)
#if defined ARCH_MAC
					|| e.isKeyCommand(GLFW_KEY_LEFT, RACK_MOD_CTRL | shift)
#endif
				) {
					moveToLineStart(shift != 0);
					e.consume(this);
					return;
				}
				if (e.isKeyCommand(GLFW_KEY_END, shift)
#if defined ARCH_MAC
					|| e.isKeyCommand(GLFW_KEY_RIGHT, RACK_MOD_CTRL | shift)
#endif
				) {
					moveToLineEnd(shift != 0);
					e.consume(this);
					return;
				}
			}
		}
		TextField::onSelectKey(e);
	}

	void draw(const DrawArgs& args) override {
		NVGcontext* vg = args.vg;
		const BNDwidgetTheme& theme = bndGetTheme()->textFieldTheme;

		nvgSave(vg);
		nvgScissor(vg, RECT_ARGS(args.clipBox));

		nvgBeginPath(vg);
		nvgRect(vg, 0.f, 0.f, box.size.x, box.size.y);
		nvgFillColor(vg, theme.innerColor);
		nvgFill(vg);

		std::vector<int> starts = scripttext::lineStarts(text);
		int nLines = (int)starts.size();
		int first = std::max(0, (int)std::floor((args.clipBox.getTop() - kPadY) / kLineHeight));
		int last = std::min(nLines - 1, (int)std::floor((args.clipBox.getBottom() - kPadY) / kLineHeight));

		int selBegin = std::min(cursor, selection);
		int selEnd = std::max(cursor, selection);
		bool active = this == APP->event->selectedWidget;

		// Find matches, below the selection and the text.
		if (!findNeedle.empty()) {
			const std::vector<int>& m = getMatches();
			int len = (int)findNeedle.size();
			nvgFillColor(vg, nvgRGBA(240, 200, 60, 80));
			for (int l = first; l <= last; l++) {
				int ls = starts[l];
				int lineEnd = l + 1 < nLines ? starts[l + 1] : (int)text.size() + 1;
				int contentEnd = scripttext::lineContentEnd(text, ls);
				for (int i = scripttext::firstMatchAtOrAfter(m, ls); i >= 0 && i < (int)m.size() && m[i] < lineEnd; i++) {
					int a = m[i];
					int b = std::min(a + len, contentEnd);
					if (b <= a) continue;
					float x0 = colToX(scripttext::countCodepoints(text, ls, a));
					float x1 = colToX(scripttext::countCodepoints(text, ls, b));
					nvgBeginPath(vg);
					nvgRect(vg, x0, kPadY + l * kLineHeight, x1 - x0, kLineHeight);
					nvgFill(vg);
				}
			}
		}

		// Selection, below the text.
		if (selBegin != selEnd) {
			nvgFillColor(vg, nvgRGBA(90, 140, 230, 110));
			for (int l = first; l <= last; l++) {
				int ls = starts[l];
				int contentEnd = scripttext::lineContentEnd(text, ls);
				int nextStart = l + 1 < nLines ? starts[l + 1] : (int)text.size() + 1;
				if (selEnd <= ls || selBegin >= nextStart) continue;
				int a = std::min(std::max(selBegin, ls), contentEnd);
				int b = std::min(selEnd, contentEnd);
				float x0 = colToX(scripttext::countCodepoints(text, ls, a));
				float x1 = colToX(scripttext::countCodepoints(text, ls, b));
				// A selection running over the line break shows as a half-cell.
				if (selEnd > contentEnd && l + 1 < nLines) x1 += charWidth() * 0.5f;
				if (x1 <= x0) continue;
				nvgBeginPath(vg);
				nvgRect(vg, x0, kPadY + l * kLineHeight, x1 - x0, kLineHeight);
				nvgFill(vg);
			}
		}

		// Text.
		std::shared_ptr<window::Font> font = APP->window->loadFont(asset::plugin(pluginInstance, "res/fonts/VictorMono-SemiBold.ttf"));
		if (font && font->handle >= 0) {
			nvgFontFaceId(vg, font->handle);
			nvgFontSize(vg, kFontSize);
			measureCharWidth(vg);
			nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
			nvgFillColor(vg, theme.textColor);
			for (int l = first; l <= last; l++) {
				int ls = starts[l];
				int le = scripttext::lineContentEnd(text, ls);
				if (le <= ls) continue;
				// One column per character: a tab would otherwise draw at an unknown width.
				std::string line = text.substr(ls, le - ls);
				std::replace(line.begin(), line.end(), '\t', ' ');
				nvgText(vg, kPadX, kPadY + (l + 0.5f) * kLineHeight, line.c_str(), NULL);
			}
		}

		// Caret.
		if (active) {
			scripttext::LineCol lc = scripttext::offsetToLineCol(text, cursor);
			nvgBeginPath(vg);
			nvgRect(vg, colToX(lc.col) - 0.5f, kPadY + lc.line * kLineHeight + 1.f, 1.f, kLineHeight - 2.f);
			nvgFillColor(vg, theme.textColor);
			nvgFill(vg);
		}

		nvgResetScissor(vg);
		nvgRestore(vg);
	}
};

// LineNumberGutter

struct LineNumberGutter : widget::TransparentWidget {
	static constexpr float kWidth = 44.f;

	ScriptEditField* field = nullptr;

	LineNumberGutter() {
		box.size.x = kWidth;
	}

	void draw(const DrawArgs& args) override {
		if (!field) return;
		NVGcontext* vg = args.vg;

		nvgBeginPath(vg);
		nvgRect(vg, 0.f, 0.f, box.size.x, box.size.y);
		nvgFillColor(vg, nvgRGBA(0, 0, 0, 60));
		nvgFill(vg);

		std::shared_ptr<window::Font> font = APP->window->loadFont(asset::plugin(pluginInstance, "res/fonts/VictorMono-SemiBold.ttf"));
		if (!font || font->handle < 0) return;
		nvgFontFaceId(vg, font->handle);
		nvgFontSize(vg, ScriptEditField::kFontSize);
		nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
		nvgFillColor(vg, nvgRGBA(160, 160, 160, 200));

		int nLines = scripttext::lineCount(field->text);
		int first = std::max(0, (int)std::floor((args.clipBox.getTop() - ScriptEditField::kPadY) / ScriptEditField::kLineHeight));
		int last = std::min(nLines - 1, (int)std::floor((args.clipBox.getBottom() - ScriptEditField::kPadY) / ScriptEditField::kLineHeight));
		for (int l = first; l <= last; l++) {
			std::string n = string::f("%d", l + 1);
			nvgText(vg, box.size.x - 8.f, ScriptEditField::kPadY + (l + 0.5f) * ScriptEditField::kLineHeight, n.c_str(), NULL);
		}
	}
};

// AlwaysScrollbarScrollWidget

// Rack's Scrollbar draws and handles clicks by dividing by the scrollable range, which
// is zero when everything fits, so it is hidden then. This one copes with an empty range
// (a full-height handle that does nothing), so the scroll widget below can keep it shown.
struct AlwaysVisibleScrollbar : rack::ui::Scrollbar {
	bool canScroll() {
		ScrollWidget* sw = dynamic_cast<ScrollWidget*>(parent);
		return sw && sw->getContainerOffsetBound().size[vertical] > 0.f;
	}

	void draw(const DrawArgs& args) override {
		if (canScroll()) {
			Scrollbar::draw(args);
			return;
		}
		nvgAlpha(args.vg, 0.5f);
		bndScrollBar(args.vg, 0.f, 0.f, box.size.x, box.size.y, BND_DEFAULT, 0.f, 1.f);
	}

	void onButton(const ButtonEvent& e) override {
		if (canScroll()) {
			Scrollbar::onButton(e);
			return;
		}
		OpaqueWidget::onButton(e);
	}

	void onDragMove(const DragMoveEvent& e) override {
		if (canScroll()) Scrollbar::onDragMove(e);
	}
};

// A ScrollWidget whose vertical scrollbar is always shown, to mark the area as scrollable
// and to keep the layout from jumping when the content outgrows the viewport.
struct AlwaysScrollbarScrollWidget : rack::ui::ScrollWidget {
	AlwaysScrollbarScrollWidget() {
		removeChild(verticalScrollbar);
		delete verticalScrollbar;
		AlwaysVisibleScrollbar* bar = new AlwaysVisibleScrollbar;
		bar->vertical = true;
		addChild(bar);
		verticalScrollbar = bar;
	}

	void step() override {
		ScrollWidget::step();
		verticalScrollbar->setVisible(true);
	}
};

// ScriptLogView

// The script's log output, mirrored from the owner's log display: one line per entry,
// oldest first, newest at the bottom. Lines that look like errors are tinted red.
struct ScriptLogView : widget::OpaqueWidget {
	static constexpr float kFontSize = 11.f;
	static constexpr float kLineHeight = 14.f;
	static constexpr float kPad = 4.f;
	static constexpr size_t kMaxLines = 500;

	// Every line received; the filter decides which of them are shown.
	std::vector<std::string> lines;
	scripttext::LineFilter filter;
	// Indices into `lines` of the shown ones, rebuilt lazily.
	std::vector<int> shown;
	bool shownStale = true;
	// Clears the area (and resets its scrolling). Set by the dialog.
	std::function<void()> clearAction;

	const std::vector<int>& getShown() {
		if (shownStale) {
			shown.clear();
			for (size_t i = 0; i < lines.size(); i++) {
				if (filter.matches(lines[i])) shown.push_back((int)i);
			}
			shownStale = false;
		}
		return shown;
	}

	void setFilter(const std::string& pattern, bool matchCase, bool useRegex) {
		filter.set(pattern, matchCase, useRegex);
		shownStale = true;
	}

	// What is on screen: the lines the filter lets through.
	std::string allText() {
		std::string r;
		bool first = true;
		for (int i : getShown()) {
			if (!first) r += "\n";
			first = false;
			r += lines[i];
		}
		return r;
	}

	void copyToClipboard() {
		vcv::ui::setClipboard(allText());
	}

	// Right-click: copy everything / clear. The area only mirrors the owner's log, so
	// clearing it leaves the owner's own log display alone.
	void onButton(const ButtonEvent& e) override {
		OpaqueWidget::onButton(e);
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_RIGHT) {
			rack::ui::Menu* menu = createMenu();
			bool empty = getShown().empty();
			menu->addChild(createMenuItem("Copy to clipboard", "", [this]() { copyToClipboard(); }, empty));
			menu->addChild(createMenuItem("Clear", "", [this]() { if (clearAction) clearAction(); }, empty));
			e.consume(this);
		}
	}

	static float contentHeight(size_t n) {
		return n * kLineHeight + 2.f * kPad;
	}

	// An entry may span several lines.
	void append(const std::string& entry) {
		size_t start = 0;
		while (true) {
			size_t nl = entry.find('\n', start);
			lines.push_back(entry.substr(start, nl == std::string::npos ? std::string::npos : nl - start));
			if (nl == std::string::npos) break;
			start = nl + 1;
		}
		if (lines.size() > kMaxLines) lines.erase(lines.begin(), lines.begin() + (lines.size() - kMaxLines));
		shownStale = true;
	}

	void draw(const DrawArgs& args) override {
		NVGcontext* vg = args.vg;
		nvgBeginPath(vg);
		nvgRect(vg, 0.f, 0.f, box.size.x, box.size.y);
		nvgFillColor(vg, nvgRGBA(0, 0, 0, 90));
		nvgFill(vg);

		std::shared_ptr<window::Font> font = APP->window->loadFont(asset::plugin(pluginInstance, "res/fonts/VictorMono-SemiBold.ttf"));
		const std::vector<int>& rows = getShown();
		if (!font || font->handle < 0 || rows.empty()) return;
		nvgScissor(vg, RECT_ARGS(args.clipBox));
		nvgFontFaceId(vg, font->handle);
		nvgFontSize(vg, kFontSize);
		nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		int n = (int)rows.size();
		int first = std::max(0, (int)std::floor((args.clipBox.getTop() - kPad) / kLineHeight));
		int last = std::min(n - 1, (int)std::floor((args.clipBox.getBottom() - kPad) / kLineHeight));
		for (int l = first; l <= last; l++) {
			nvgFillColor(vg, scripttext::looksLikeError(lines[rows[l]]) ? nvgRGB(240, 100, 90) : nvgRGBA(200, 200, 200, 230));
			nvgText(vg, 6.f, kPad + (l + 0.5f) * kLineHeight, lines[rows[l]].c_str(), NULL);
		}
		nvgResetScissor(vg);
	}
};

// SplitHandle

// A thin bar the user drags to trade space between the code area and the log area
// below it; the dialog keeps its size.
struct SplitHandle : widget::OpaqueWidget {
	static constexpr float kHeight = 8.f;

	// Receives the vertical drag distance in dialog pixels, positive downwards.
	std::function<void(float)> dragged;

	SplitHandle() {
		box.size.y = kHeight;
	}

	void onDragStart(const DragStartEvent& e) override {
		if (e.button == GLFW_MOUSE_BUTTON_LEFT) e.consume(this);
	}

	void onDragMove(const DragMoveEvent& e) override {
		if (e.button != GLFW_MOUSE_BUTTON_LEFT) return;
		if (dragged) dragged(e.mouseDelta.div(getAbsoluteZoom()).y);
	}

	void draw(const DrawArgs& args) override {
		bool active = this == APP->event->hoveredWidget || this == APP->event->draggedWidget;
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, box.size.x * 0.5f - 24.f, box.size.y * 0.5f - 1.f, 48.f, 2.f, 1.f);
		nvgFillColor(args.vg, nvgRGBA(255, 255, 255, active ? 150 : 70));
		nvgFill(args.vg);
	}
};

// ScriptEditorHost

// The owner's side of the editor: everything the editor needs from whoever owns the
// script. The dialog knows only this interface, so it stays independent of any one
// module or engine, and new owner-side features (error feedback, say) extend it with a
// defaulted virtual instead of another constructor argument. The dialog owns the host:
// it lives as long as the editor and is destroyed with it, so it may hold raw pointers
// to the owner as long as the owner dismisses the editor before it goes away.
struct ScriptEditorHost {
	virtual ~ScriptEditorHost() {}

	// Apply the buffer: make the owner run this text. Called for Apply and Apply & Close.
	virtual void apply(const std::string& text) = 0;

	// The text the owner is running right now. Revert reloads the buffer from it.
	virtual std::string runningScript() = 0;

	// Optional suffix for the header, e.g. the engine name: "Script â <suffix>".
	// Polled each frame, so it stays right after an apply switches the engine.
	virtual std::string headerSuffix() { return ""; }

	// The editor mirrors the owner's log output in its log area: the owner calls `append`
	// with each new entry (it may span lines) and `clear` when its log is reset. Called
	// once, when the editor opens; the owner replays what it already has. The callbacks
	// are valid for as long as the host lives, which is as long as the dialog does.
	virtual void attachLog(std::function<void(const std::string&)> append, std::function<void()> clear) {}

	// The editor closed itself (Close, Apply & Close, Esc). Not called when the owner
	// dismisses it.
	virtual void onEditorClosed() {}
};

// Buttons

struct ActionButton : rack::ui::Button {
	std::function<void()> action;
	void onAction(const ActionEvent& e) override {
		if (action) action();
	}
};

// A button that stays lit while its option is on.
struct ToggleButton : ActionButton {
	bool on = false;
	void draw(const DrawArgs& args) override {
		ActionButton::draw(args);
		if (on) {
			nvgBeginPath(args.vg);
			nvgRoundedRect(args.vg, 0.f, 0.f, box.size.x, box.size.y, 4.f);
			nvgFillColor(args.vg, nvgRGBA(240, 200, 60, 90));
			nvgFill(args.vg);
		}
	}
};

// FindField

// The find bar's input: Enter / Shift+Enter (and F3) step through the matches, Esc closes
// the bar. Its text is the search, so every change re-runs it.
struct FindField : TextField {
	std::function<void(bool forward)> nextAction;
	std::function<void()> closeAction;
	std::function<void()> changeAction;

	void onChange(const ChangeEvent& e) override {
		TextField::onChange(e);
		if (changeAction) changeAction();
	}

	void onSelectKey(const SelectKeyEvent& e) override {
		if (e.action == GLFW_PRESS || e.action == GLFW_REPEAT) {
			bool enter = e.key == GLFW_KEY_ENTER || e.key == GLFW_KEY_KP_ENTER;
			if ((enter && (e.isKeyCommand(e.key) || e.isKeyCommand(e.key, GLFW_MOD_SHIFT)))
				|| e.isKeyCommand(GLFW_KEY_F3) || e.isKeyCommand(GLFW_KEY_F3, GLFW_MOD_SHIFT)) {
				if (nextAction) nextAction((e.mods & GLFW_MOD_SHIFT) == 0);
				e.consume(this);
				return;
			}
			if (e.isKeyCommand(GLFW_KEY_ESCAPE)) {
				if (e.action == GLFW_PRESS && closeAction) closeAction();
				e.consume(this);
				return;
			}
			// Ctrl+F in the bar selects the search again.
			if (e.isKeyCommand(GLFW_KEY_F, RACK_MOD_CTRL)) {
				selectAll();
				e.consume(this);
				return;
			}
			// The bar has no undo, and the key must not reach Rack's patch history.
			if (e.key == GLFW_KEY_Z || e.key == GLFW_KEY_Y) {
				if (e.isKeyCommand(e.key, RACK_MOD_CTRL) || e.isKeyCommand(e.key, RACK_MOD_CTRL | GLFW_MOD_SHIFT)) {
					e.consume(this);
					return;
				}
			}
		}
		TextField::onSelectKey(e);
	}
};

// ScriptLogPanel

// The log area as one widget: the mirrored log (ScriptLogView in a scroll widget) with its
// line filter in a row below it. The dialog only positions it and feeds it entries.
struct ScriptLogPanel : widget::Widget {
	static constexpr float kFilterBarHeight = 28.f;
	static constexpr float kScrollbarWidth = 20.f;

	rack::ui::ScrollWidget* scroll;
	ScriptLogView* view;
	widget::Widget* filterBar;
	FindField* filterInput;
	rack::ui::Label* filterCount;
	ToggleButton* filterCaseButton;
	ToggleButton* filterRegexButton;
	// Enter and Esc in the filter input give the keyboard back to the editor.
	std::function<void()> returnFocus;

	ScriptLogPanel() {
		scroll = new AlwaysScrollbarScrollWidget;
		addChild(scroll);
		view = new ScriptLogView;
		view->clearAction = [this]() { clear(); };
		scroll->container->addChild(view);

		filterBar = new widget::Widget;
		addChild(filterBar);

		// Same single-line input as the find bar.
		filterInput = new FindField;
		filterInput->placeholder = "Filter log lines";
		filterInput->nextAction = [this](bool) { if (returnFocus) returnFocus(); };
		filterInput->closeAction = [this]() { if (returnFocus) returnFocus(); };
		filterInput->changeAction = [this]() { filterChanged(); };
		filterBar->addChild(filterInput);

		filterCount = new rack::ui::Label;
		filterCount->box.size = Vec(120.f, 20.f);
		filterCount->alignment = rack::ui::Label::RIGHT_ALIGNMENT;
		filterBar->addChild(filterCount);

		filterCaseButton = new ToggleButton;
		filterCaseButton->text = "Aa";
		filterCaseButton->action = [this]() {
			filterCaseButton->on = !filterCaseButton->on;
			filterChanged();
		};
		filterBar->addChild(filterCaseButton);

		filterRegexButton = new ToggleButton;
		filterRegexButton->text = ".*";
		filterRegexButton->action = [this]() {
			filterRegexButton->on = !filterRegexButton->on;
			filterChanged();
		};
		filterBar->addChild(filterRegexButton);
	}

	// Lays the parts out inside box.size: the log on top, the filter row at the bottom.
	void layout() {
		scroll->box.pos = Vec();
		scroll->box.size = Vec(box.size.x, std::max(0.f, box.size.y - kFilterBarHeight));
		filterBar->box.pos = Vec(0.f, scroll->box.size.y);
		filterBar->box.size = Vec(box.size.x, kFilterBarHeight);
		const float y = 4.f;
		float x = filterBar->box.size.x;
		x -= 36.f;
		filterRegexButton->box.size.x = 36.f;
		filterRegexButton->box.pos = Vec(x, y);
		x -= 4.f + 36.f;
		filterCaseButton->box.size.x = 36.f;
		filterCaseButton->box.pos = Vec(x, y);
		x -= 4.f + filterCount->box.size.x;
		filterCount->box.pos = Vec(x, y);
		filterInput->box.pos = Vec(0.f, y);
		filterInput->box.size.x = std::max(60.f, x - 8.f);
	}

	// The owner mirrors its log output here. Follows the newest line unless the user has
	// scrolled up to read earlier ones.
	void append(const std::string& entry) {
		bool atBottom = scroll->offset.y >= scroll->getContainerOffsetBound().getBottom() - 2.f;
		view->append(entry);
		if (atBottom) scroll->offset.y = INFINITY;   // clamped to the bottom by the scroll widget
	}

	// Empties the log; the filter stays for what comes next.
	void clear() {
		view->lines.clear();
		view->shownStale = true;
		scroll->offset = Vec();
	}

	void filterChanged() {
		view->setFilter(filterInput->text, filterCaseButton->on, filterRegexButton->on);
		scroll->offset.y = INFINITY;   // back to the newest line, whatever is left
		updateCount();
	}

	void updateCount() {
		std::string t;
		if (view->filter.active()) {
			if (!view->filter.isValid()) t = "Invalid regex";
			else t = string::f("%d / %d", (int)view->getShown().size(), (int)view->lines.size());
		}
		if (filterCount->text != t) filterCount->text = t;
	}

	void step() override {
		// The view spans the viewport at least, and grows with the shown lines.
		Vec viewport = scroll->box.size;
		view->box.size = Vec(viewport.x - kScrollbarWidth, std::max(viewport.y, ScriptLogView::contentHeight(view->getShown().size())));
		updateCount();
		Widget::step();
	}
};

// ScriptFindBar

// The find bar: a search input with next / previous / match-case controls and a result
// count, shown between the dialog's header and the code area. It searches the editor
// field it is given, highlighting matches there and selecting the current one; the field
// scrolls to the selection by itself. Hidden until opened.
struct ScriptFindBar : widget::Widget {
	static constexpr float kHeight = 34.f;

	ScriptEditField* field;
	FindField* input;
	rack::ui::Label* count;
	ToggleButton* caseButton;
	ActionButton* prevButton;
	ActionButton* nextButton;
	ActionButton* closeButton;
	bool isOpen = false;
	// The bar appeared or went away: the owner re-lays out around it.
	std::function<void()> layoutChanged;
	// Esc and the close button give the keyboard back to the editor.
	std::function<void()> returnFocus;

	explicit ScriptFindBar(ScriptEditField* f) : field(f) {
		hide();

		input = new FindField;
		input->placeholder = "Find";
		input->nextAction = [this](bool forward) { findStep(forward); };
		input->closeAction = [this]() { close(); };
		input->changeAction = [this]() { changed(); };
		addChild(input);

		count = new rack::ui::Label;
		count->box.size = Vec(110.f, 20.f);
		count->alignment = rack::ui::Label::RIGHT_ALIGNMENT;
		addChild(count);

		caseButton = new ToggleButton;
		caseButton->text = "Aa";
		caseButton->action = [this]() {
			caseButton->on = !caseButton->on;
			changed();
		};
		addChild(caseButton);

		prevButton = addButton("Prev", [this]() { findStep(false); });
		nextButton = addButton("Next", [this]() { findStep(true); });
		closeButton = addButton("X", [this]() { close(); });
	}

	// The height the bar takes from the dialog: none while closed.
	float space() const {
		float h = 0.f;
		if (isOpen) h = kHeight;
		return h;
	}

	// Right to left inside box.size; the input takes what is left.
	void layout() {
		setVisible(isOpen);
		const float y = 6.f;
		float x = box.size.x;
		x -= 30.f;
		closeButton->box.size.x = 30.f;
		closeButton->box.pos = Vec(x, y);
		x -= 4.f + 60.f;
		nextButton->box.size.x = 60.f;
		nextButton->box.pos = Vec(x, y);
		x -= 4.f + 60.f;
		prevButton->box.size.x = 60.f;
		prevButton->box.pos = Vec(x, y);
		x -= 4.f + 40.f;
		caseButton->box.size.x = 40.f;
		caseButton->box.pos = Vec(x, y);
		x -= 4.f + count->box.size.x;
		count->box.pos = Vec(x, y);
		input->box.pos = Vec(0.f, y);
		input->box.size.x = std::max(60.f, x - 8.f);
	}

	// Ctrl+F: show the bar with focus in the input. A one-line selection in the editor
	// becomes the search.
	void open() {
		show();
		if (field->cursor != field->selection) {
			std::string sel = field->getSelectedText();
			if (sel.find('\n') == std::string::npos) input->setText(sel);
		}
		changed();
		input->selectAll();
		APP->event->setSelectedWidget(input);
	}

	void close() {
		if (!isOpen) return;
		isOpen = false;
		field->setFind("", caseButton->on);
		if (layoutChanged) layoutChanged();
		if (returnFocus) returnFocus();
	}

	// F3 in the editor: step through the last search, bringing the bar back if it was
	// closed. Focus stays in the editor. With no search yet it opens the bar instead.
	void stepFromEditor(bool forward) {
		if (!isOpen) {
			if (input->text.empty()) {
				open();
				return;
			}
			show();
			field->setFind(input->text, caseButton->on);
		}
		findStep(forward);
	}

	// Next / previous match, wrapping around the ends.
	void findStep(bool forward) {
		const std::vector<int>& m = field->getMatches();
		if (!m.empty()) {
			int idx;
			if (forward) {
				idx = scripttext::firstMatchAtOrAfter(m, std::max(field->cursor, field->selection));
				if (idx < 0) idx = 0;
			}
			else {
				idx = scripttext::lastMatchBefore(m, std::min(field->cursor, field->selection));
				if (idx < 0) idx = (int)m.size() - 1;
			}
			field->selectMatch(idx);
		}
		updateCount();
	}

	void updateCount() {
		std::string t;
		if (!field->findNeedle.empty()) {
			int n = (int)field->getMatches().size();
			int cur = field->currentMatchIndex();
			if (n == 0) t = "No results";
			else if (cur >= 0) t = string::f("%d of %d", cur + 1, n);
			else t = string::f("%d matches", n);
		}
		if (count->text != t) count->text = t;
	}

	void step() override {
		if (isOpen) updateCount();
		Widget::step();
	}

private:
	ActionButton* addButton(const std::string& label, std::function<void()> action) {
		ActionButton* b = new ActionButton;
		b->text = label;
		b->action = std::move(action);
		addChild(b);
		return b;
	}

	// The search changed: highlight it, and move to the first match from the current
	// position (incremental search). The editor does not take focus.
	void changed() {
		field->setFind(input->text, caseButton->on);
		const std::vector<int>& m = field->getMatches();
		if (!m.empty()) {
			int idx = scripttext::firstMatchAtOrAfter(m, std::min(field->cursor, field->selection));
			field->selectMatch(idx >= 0 ? idx : 0);
		}
		updateCount();
	}

	void show() {
		if (isOpen) return;
		isOpen = true;
		if (layoutChanged) layoutChanged();
	}
};

// ScriptEditorDialog

struct ScriptEditorDialog : widget::OpaqueWidget {
	static constexpr float kMargin = 10.f;
	static constexpr float kHeaderHeight = 30.f;
	static constexpr float kFooterHeight = 40.f;
	static constexpr float kDefaultLogHeight = 158.f;   // log area below the footer, incl. bottom margin
	static constexpr float kMinLogHeight = 88.f;
	static constexpr float kMinCodeHeight = 150.f;
	static constexpr float kMaxWidth = 1050.f;
	static constexpr float kMaxHeight = 928.f;
	static constexpr float kScrollbarWidth = 20.f;

	// The owner's side of the editor. Null once the owner has been cut off (dismiss()),
	// after which Apply and Revert only touch the buffer.
	std::unique_ptr<ScriptEditorHost> host;

	ScriptEditField* field;
	LineNumberGutter* gutter;
	rack::ui::ScrollWidget* scroll;
	rack::ui::Label* headerLabel;
	rack::ui::Label* modifiedLabel;
	rack::ui::Label* statusLabel;
	ScriptLogPanel* logPanel;
	SplitHandle* splitHandle;
	ScriptFindBar* findBar;
	// Height of the log area; the user drags it. Not persisted.
	float logHeight = kDefaultLogHeight;
	std::vector<ActionButton*> buttons;

	// The text the module is running as far as the editor knows: the text it was
	// opened with, or what was last applied.
	std::string baseline;
	bool dirty = false;
	bool closing = false;

	ScriptEditorDialog(const std::string& text, std::unique_ptr<ScriptEditorHost> hostIn) {
		box.size = Vec(kMaxWidth, kMaxHeight);
		host = std::move(hostIn);
		baseline = text;

		headerLabel = new rack::ui::Label;
		headerLabel->box.pos = Vec(kMargin, 6.f);
		headerLabel->box.size = Vec(300.f, 20.f);
		headerLabel->text = "Script";
		addChild(headerLabel);

		modifiedLabel = new rack::ui::Label;
		modifiedLabel->box.size = Vec(120.f, 20.f);
		modifiedLabel->box.pos = Vec(box.size.x - kMargin - modifiedLabel->box.size.x, 6.f);
		modifiedLabel->alignment = rack::ui::Label::RIGHT_ALIGNMENT;
		addChild(modifiedLabel);

		scroll = new AlwaysScrollbarScrollWidget;
		scroll->box.pos = Vec(kMargin, kHeaderHeight);
		scroll->box.size = Vec(box.size.x - 2.f * kMargin, box.size.y - kHeaderHeight - kFooterHeight - logHeight);
		addChild(scroll);

		field = new ScriptEditField;
		field->box.pos = Vec(LineNumberGutter::kWidth, 0.f);
		field->setText(text);
		field->cursor = field->selection = 0;
		field->clearHistory();
		field->applyAction = [this](bool closeAfter) { closeAfter ? applyAndClose() : apply(); };
		field->closeAction = [this]() { requestClose(); };
		field->changeAction = [this]() { updateDirty(); };
		field->findAction = [this]() { findBar->open(); };
		field->findNextAction = [this](bool forward) { findBar->stepFromEditor(forward); };
		scroll->container->addChild(field);

		findBar = new ScriptFindBar(field);
		findBar->layoutChanged = [this]() { layoutFrame(); };
		findBar->returnFocus = [this]() { focusField(); };
		addChild(findBar);

		gutter = new LineNumberGutter;
		gutter->field = field;
		scroll->container->addChild(gutter);

		logPanel = new ScriptLogPanel;
		logPanel->returnFocus = [this]() { focusField(); };
		addChild(logPanel);
		splitHandle = new SplitHandle;
		splitHandle->dragged = [this](float dy) { setLogHeight(logHeight - dy); };
		addChild(splitHandle);

		// Right to left: Close, Apply & Close, Apply, Revert.
		addButton("Close", 80.f, [this]() { requestClose(); });
		addButton("Apply & Close", 110.f, [this]() { applyAndClose(); });
		addButton("Apply", 80.f, [this]() { apply(); });
		addButton("Revert", 80.f, [this]() { revert(); });

		statusLabel = new rack::ui::Label;
		statusLabel->box.size.y = 20.f;
		statusLabel->text = "";
		addChild(statusLabel);

		layoutFrame();
		layoutBody();
		if (host) {
			host->attachLog([this](const std::string& entry) { appendLog(entry); }, [this]() { clearLog(); });
		}
		updateDirty();
	}

	// Keeps both areas usable: the log at least a few lines, the code area at least its minimum.
	float clampLogHeight(float h) const {
		const float minLog = kMinLogHeight;   // by value: std::min/max would odr-use the members
		float maxH = box.size.y - kHeaderHeight - findBar->space() - kFooterHeight - kMinCodeHeight;
		return std::max(std::min(minLog, maxH), std::min(h, maxH));
	}

	void setLogHeight(float h) {
		logHeight = clampLogHeight(h);
		layoutFrame();
	}

	// Positions everything from box.size, which step() clamps to the scene.
	void layoutFrame() {
		logHeight = clampLogHeight(logHeight);
		const float findSpace = findBar->space();
		modifiedLabel->box.pos = Vec(box.size.x - kMargin - modifiedLabel->box.size.x, 6.f);
		scroll->box.pos = Vec(kMargin, kHeaderHeight + findSpace);
		scroll->box.size = Vec(box.size.x - 2.f * kMargin, box.size.y - kHeaderHeight - findSpace - kFooterHeight - logHeight);
		findBar->box.pos = Vec(kMargin, kHeaderHeight);
		findBar->box.size = Vec(box.size.x - 2.f * kMargin, ScriptFindBar::kHeight);
		findBar->layout();
		const float footerY = box.size.y - logHeight - kFooterHeight + 7.f;
		logPanel->box.pos = Vec(kMargin, box.size.y - logHeight);
		logPanel->box.size = Vec(box.size.x - 2.f * kMargin, logHeight - kMargin);
		logPanel->layout();
		splitHandle->box.pos = Vec(kMargin, box.size.y - logHeight - SplitHandle::kHeight + 2.f);
		splitHandle->box.size.x = box.size.x - 2.f * kMargin;
		float x = box.size.x - kMargin;
		for (ActionButton* b : buttons) {
			x -= b->box.size.x;
			b->box.pos = Vec(x, footerY);
			x -= 8.f;
		}
		statusLabel->box.pos = Vec(kMargin, footerY + 3.f);
		statusLabel->box.size.x = std::max(0.f, x - kMargin);
	}

	void addButton(const std::string& label, float width, std::function<void()> action) {
		ActionButton* b = new ActionButton;
		buttons.push_back(b);
		b->text = label;
		b->box.size.x = width;
		b->action = std::move(action);
		addChild(b);
	}

	void updateDirty() {
		dirty = field->text != baseline;
		modifiedLabel->text = dirty ? "modified" : "";
	}

	std::string getText() const {
		return field->text;
	}

	// The field spans the viewport at least, so clicks below the last line still land in it.
	void layoutBody() {
		Vec viewport = scroll->box.size;
		float h = std::max(viewport.y, ScriptEditField::contentHeight(scripttext::lineCount(field->text)));
		field->box.size = Vec(viewport.x - LineNumberGutter::kWidth - kScrollbarWidth, h);
		gutter->box.size = Vec(LineNumberGutter::kWidth, h);
	}

	// The owner mirrors its log output here (see ScriptEditorHost::attachLog).
	void appendLog(const std::string& entry) {
		logPanel->append(entry);
	}

	void clearLog() {
		logPanel->clear();
	}

	void focusField() {
		APP->event->setSelectedWidget(field);
	}

	void apply() {
		std::string text = field->text;
		if (host) host->apply(text);
		baseline = text;
		updateDirty();
		focusField();
	}

	// Discards the buffer and shows the running script again.
	void revert() {
		if (host) baseline = host->runningScript();
		field->setText(baseline);
		field->cursor = field->selection = 0;
		field->preferredCol = -1;
		field->lastCursor = -1;
		updateDirty();
		focusField();
	}

	void applyAndClose() {
		apply();
		close();
	}

	// Close through here from every user path: it asks before discarding edits.
	void requestClose() {
		if (closing) return;
		if (dirty && !vcv::ui::message(vcv::MessageType::WARNING, vcv::MessageButtons::YES_NO, "Discard unapplied changes?")) {
			focusField();
			return;
		}
		close();
	}

	void close() {
		if (closing) return;
		closing = true;
		if (host) host->onEditorClosed();
		if (parent) parent->requestDelete();
	}

	void step() override {
		if (host) {
			std::string suffix = host->headerSuffix();
			std::string header = suffix.empty() ? "Script" : "Script \xE2\x80\x94 " + suffix;
			if (headerLabel->text != header) headerLabel->text = header;
		}
		// Fixed size, except that it never outgrows a small window.
		const float maxW = kMaxWidth, maxH = kMaxHeight;   // by value: std::min would odr-use the members
		Vec size(std::min(maxW, parent->box.size.x - 40.f), std::min(maxH, parent->box.size.y - 40.f));
		if (size.x > 0.f && size.y > 0.f && !size.isEqual(box.size)) {
			box.size = size;
			layoutFrame();
		}
		box.pos = parent->box.size.minus(box.size).div(2).round();
		layoutBody();

		scripttext::LineCol lc = scripttext::offsetToLineCol(field->text, field->cursor);
		std::string status = string::f("Ln %d, Col %d", lc.line + 1, lc.col + 1);
		if (statusLabel->text != status) statusLabel->text = status;

		OpaqueWidget::step();
	}

	void draw(const DrawArgs& args) override {
		bndMenuBackground(args.vg, 0.f, 0.f, box.size.x, box.size.y, 0);
		if (dirty) {
			nvgBeginPath(args.vg);
			nvgCircle(args.vg, modifiedLabel->box.pos.x + modifiedLabel->box.size.x - 62.f, 16.f, 3.5f);
			nvgFillColor(args.vg, nvgRGB(230, 160, 40));
			nvgFill(args.vg);
		}
		Widget::draw(args);
	}
};

// ScriptEditorOverlay
// ui::MenuOverlay deletes itself on any click that reaches it (i.e. outside the
// dialog) and on Esc. For an editor holding unapplied text that must go
// through the dialog's close guard instead.

struct ScriptEditorOverlay : rack::ui::MenuOverlay {
	ScriptEditorDialog* dialog = nullptr;

	void onAction(const ActionEvent& e) override {
		if (dialog) dialog->requestClose();
	}

	// Only a left click outside the dialog asks to close. MenuOverlay takes any press
	// that reaches it for that, and OpaqueWidget lets right and middle clicks inside
	// the dialog through unconsumed, so a right-click on the dialog's background
	// would otherwise close the editor.
	void onButton(const ButtonEvent& e) override {
		if (e.button == GLFW_MOUSE_BUTTON_LEFT) {
			MenuOverlay::onButton(e);
			return;
		}
		OpaqueWidget::onButton(e);
		if (!e.isConsumed()) e.consume(this);
	}

	// A click outside the dialog makes Rack select this overlay after onAction() has
	// run, so the field gets its focus back on the next frame.
	void step() override {
		MenuOverlay::step();
		if (dialog && !dialog->closing && APP->event->selectedWidget == this) {
			dialog->focusField();
		}
	}

	// Removal from outside, e.g. the owner is going away: no close guard, and the
	// dialog's host is cut off since the deletion only happens next frame.
	void dismiss() {
		if (dialog) {
			dialog->host.reset();
		}
		requestDelete();
	}
};

// Opens the editor on a copy of `text` and returns its overlay. The dialog takes
// ownership of `host`. The overlay is weak-referenceable, so owners can simply hold a
// WeakPtr and read null once it is gone.
inline ScriptEditorOverlay* openScriptEditor(const std::string& text, std::unique_ptr<ScriptEditorHost> host) {
	ScriptEditorOverlay* overlay = new ScriptEditorOverlay;
	overlay->bgColor = nvgRGBAf(0.f, 0.f, 0.f, 0.5f);
	ScriptEditorDialog* dialog = new ScriptEditorDialog(text, std::move(host));
	overlay->dialog = dialog;
	overlay->addChild(dialog);
	APP->scene->addChild(overlay);
	dialog->focusField();
	return overlay;
}

} // namespace editor
} // namespace ui
} // namespace StoermelderPackOne