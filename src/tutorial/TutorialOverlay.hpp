#pragma once
// Requires Tutorial.hpp's Target/Step/Tutorial to already be defined — always
// #include "Tutorial.hpp" (never this file directly); it includes this one at the point
// where TutorialOverlay's definition needs it. See Tutorial.hpp's own comment.
#include "../plugin.hpp"
#include "../vcv/ui.hpp"
#include "TutorialPlacement.hpp"
#include <cfloat>


namespace StoermelderPackOne {
namespace Tutorial {

// What a TutorialBubble's buttons call back into. Satisfied by TutorialOverlay; kept as its
// own interface so the bubble never needs to know about the overlay, the module, or the rack —
// and so a test can stand in a lightweight stub for it.
struct TutorialBubbleHost {
	virtual ~TutorialBubbleHost() {}
	virtual void next() = 0;
	virtual void back() = 0;
	virtual void close() = 0;
};


// A tutorial step's dialog: caption + close button, title, word-wrapped body, and Back/Next
// (or Finish) buttons. Lives as a child of APP->scene->rack (positioned there by
// TutorialOverlay, not by itself); has no knowledge of the module or the placement it was
// computed from beyond the geometry pushed into it via setContent()/setPointer().
struct TutorialBubble : widget::OpaqueWidget {
	// ui::Button draws through bndToolButton(), which hardcodes Blendish's own
	// BND_WIDGET_HEIGHT (21px) and BND_LABEL_FONT_SIZE (13px) — oversized next to this bubble's
	// own fonts (9-12px) and width (~140px). Self-drawn instead, at Style::bodyFontSize.
	struct BubbleButton : ui::Button {
		TutorialBubbleHost* host = nullptr;
		float fontSize = 10.5f;

		void draw(const DrawArgs& args) override {
			bool hovered = APP->event->getHoveredWidget() == this;
			NVGcolor fill = hovered ? bndGetTheme()->regularTheme.innerSelectedColor
			                        : bndGetTheme()->regularTheme.innerColor;

			nvgBeginPath(args.vg);
			nvgRoundedRect(args.vg, 0.f, 0.f, box.size.x, box.size.y, 2.f);
			nvgFillColor(args.vg, fill);
			nvgFill(args.vg);
			nvgStrokeColor(args.vg, bndGetTheme()->regularTheme.outlineColor);
			nvgStrokeWidth(args.vg, 1.f);
			nvgStroke(args.vg);

			nvgFontFaceId(args.vg, APP->window->uiFont->handle);
			nvgFontSize(args.vg, fontSize);
			nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
			nvgFillColor(args.vg, bndGetTheme()->regularTheme.textColor);
			nvgText(args.vg, box.size.x * 0.5f, box.size.y * 0.5f, text.c_str(), nullptr);
		}
	};
	struct BackButton : BubbleButton {
		void onAction(const ActionEvent& e) override {
			if (host) host->back();
		}
	};
	struct NextButton : BubbleButton {
		void onAction(const ActionEvent& e) override {
			if (host) host->next();
		}
	};
	struct CloseButton : BubbleButton {
		void onAction(const ActionEvent& e) override {
			if (host) host->close();
		}
	};

	// ui::Label doesn't word-wrap in a way that matches this bubble's own measureTextBox()
	// sizing (bndIconLabelValue offsets and wraps differently), so a box laid out from that
	// estimate is too short and the last line spills out past it. Self-drawn instead: nvgTextBox
	// at NVG_ALIGN_TOP, x=0/y=0, mirroring exactly what measureTextBox() measured this text at.
	struct TutorialLabel : widget::Widget {
		std::string text;
		float fontSize = 10.f;
		NVGcolor color = color::WHITE;
		// Matches measureTextBox()'s own convention (0 = unbounded, no wrap) rather than always
		// wrapping at box.size.x: the caption is measured unbounded (it's meant to fit one
		// line) and must draw exactly as measured, even though its box is narrower for layout
		// (close button clearance) than its unwrapped text width.
		bool wrap = true;

		void draw(const DrawArgs& args) override {
			nvgFontFaceId(args.vg, APP->window->uiFont->handle);
			nvgFontSize(args.vg, fontSize);
			nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
			nvgTextLineHeight(args.vg, 1.2f);
			nvgFillColor(args.vg, color);
			float breakWidth = wrap ? box.size.x : FLT_MAX;
			nvgTextBox(args.vg, 0.f, 0.f, breakWidth, text.c_str(), nullptr);
		}
	};
	struct CaptionLabel : TutorialLabel {};
	struct TitleLabel : TutorialLabel {};
	struct BodyLabel : TutorialLabel {};

	TutorialBubbleHost* host = nullptr;
	Style style;

	CaptionLabel* captionLabel;
	TitleLabel* titleLabel;
	BodyLabel* bodyLabel;
	BackButton* backButton;
	NextButton* nextButton;
	CloseButton* closeButton;

	// Pointer geometry, in this widget's OWN local coordinates (box-relative). Set by
	// setPointer(); (0,0,0) / hasPointer == false draws no arrow, for a centered step.
	bool hasPointer = false;
	Side pointerSide = Side::AUTO;
	math::Vec pointerTip;
	math::Vec pointerBaseA;
	math::Vec pointerBaseB;

	// setContent()'s cache key: the bubble caches its size by (index, width).
	int cachedIndex = -1;
	float cachedWidth = -1.f;

	TutorialBubble() {
		captionLabel = new CaptionLabel;
		captionLabel->fontSize = style.captionFontSize;
		captionLabel->color = bndGetTheme()->regularTheme.textColor;
		captionLabel->wrap = false;
		addChild(captionLabel);

		titleLabel = new TitleLabel;
		titleLabel->fontSize = style.titleFontSize;
		titleLabel->color = bndGetTheme()->regularTheme.textColor;
		addChild(titleLabel);

		bodyLabel = new BodyLabel;
		bodyLabel->fontSize = style.bodyFontSize;
		bodyLabel->color = bndGetTheme()->regularTheme.textColor;
		addChild(bodyLabel);

		backButton = new BackButton;
		backButton->host = host;
		backButton->text = "Back";
		backButton->fontSize = style.bodyFontSize;
		addChild(backButton);

		nextButton = new NextButton;
		nextButton->host = host;
		nextButton->text = "Next";
		nextButton->fontSize = style.bodyFontSize;
		addChild(nextButton);

		closeButton = new CloseButton;
		closeButton->host = host;
		closeButton->text = "✖";
		closeButton->fontSize = style.captionFontSize;
		addChild(closeButton);
	}

	void setHost(TutorialBubbleHost* h) {
		host = h;
		backButton->host = h;
		nextButton->host = h;
		closeButton->host = h;
	}

	// Sets this step's text content and button state, and lays the bubble out to fit it at
	// `width` (module px; Style::bubbleWidth if the step didn't override it). Caches by
	// (index, width): a re-layout with the same key is a no-op, so a caller can call this every
	// step() without re-measuring text that hasn't changed.
	void setContent(int index, const std::string& caption, const std::string& title,
			const std::string& text, bool showBack, bool isLast, float width) {

		if (index == cachedIndex && math::isNear(width, cachedWidth, 1e-6f)
				&& caption == captionLabel->text && title == titleLabel->text && text == bodyLabel->text
				&& backButton->visible == showBack) {
			return;
		}
		cachedIndex = index;
		cachedWidth = width;

		captionLabel->text = caption;
		titleLabel->text = title;
		bodyLabel->text = text;
		backButton->visible = showBack;
		nextButton->text = isLast ? "Finish" : "Next";

		layout(width);
	}

	// `side` is the edge of THIS BUBBLE the notch is spliced into — the opposite of
	// Placement::side (which side of the TARGET the bubble was placed on). The caller
	// (TutorialOverlay) is responsible for that translation via oppositeSide(); passing
	// Placement::side directly here splices the notch into the wrong edge using coordinates
	// only valid on the near edge, producing a path that zigzags across the whole bubble.
	//
	// tip/baseA/baseB are given relative to the BODY's own top-left (0,0) — i.e. before any
	// bodyOffset shift below — since that's what the placement engine's coordinates mean to
	// the caller, which positions box.pos so the body lands where the placement engine
	// computed it in rack coordinates. Extends `box` so the triangle is drawn and culled
	// together with the rest of the bubble.
	void setPointer(bool has, Side side, math::Vec tip, math::Vec baseA, math::Vec baseB) {
		hasPointer = has;
		pointerSide = side;

		math::Rect body(math::Vec(), bodySize);
		if (!has) {
			box.size = bodySize;
			bodyOffset = math::Vec();
			pointerTip = tip;
			pointerBaseA = baseA;
			pointerBaseB = baseB;
			positionChildren();
			return;
		}
		math::Rect withPointer = body.expand(math::Rect::fromCorners(tip, tip));
		box.size = withPointer.size;
		// If the pointer extends past the body's top/left, the body (and everything measured
		// relative to it, including these three points) shifts within box by the same amount.
		math::Vec shift = body.pos.minus(withPointer.pos);
		bodyOffset = shift;
		pointerTip = tip.plus(shift);
		pointerBaseA = baseA.plus(shift);
		pointerBaseB = baseB.plus(shift);
		positionChildren();
	}

	math::Vec getBodySize() const { return bodySize; }

	// The body's top-left within `box` — (0,0) unless setPointer() grew the box left/up to fit
	// the triangle. The caller positions `box.pos` so that `box.pos + getBodyOffset()` lands at
	// the body's intended position, since the body — not the box's own origin — is what the
	// placement engine positioned (see setPointer()'s comment).
	math::Vec getBodyOffset() const { return bodyOffset; }

private:
	math::Vec bodySize;
	math::Vec bodyOffset; // body's top-left within box, once a pointer may have grown box left/up

	// Measurements from the last layout(), reused by positionChildren() so setPointer() can
	// shift the whole body (when the pointer grows the box up/left) without re-measuring text.
	float measuredCaptionH = 0.f, measuredTitleH = 0.f, measuredBodyH = 0.f;
	float measuredWidth = 0.f;

	void layout(float width) {
		measuredWidth = width;
		float pad = style.padding;
		float innerWidth = width - 2.f * pad;

		measuredCaptionH = vcv::ui::measureTextBox(captionLabel->text, style.captionFontSize, 0.f).y;
		measuredTitleH = vcv::ui::measureTextBox(titleLabel->text, style.titleFontSize, innerWidth).y;
		// +6px slack: nvgTextBoxBounds() and the actual nvgTextBox() draw call agree on the
		// per-line advance but not exactly on the last line's bottom bound, so a fixed pad is
		// more robust than chasing exact agreement between the two nanovg code paths.
		measuredBodyH = vcv::ui::measureTextBox(bodyLabel->text, style.bodyFontSize, innerWidth).y + 6.f;

		float y = pad + measuredCaptionH + pad * 0.5f + measuredTitleH + pad * 0.5f + measuredBodyH + pad
			+ style.buttonHeight + pad;

		bodySize = math::Vec(width, y);
		bodyOffset = math::Vec();
		box.size = bodySize;
		positionChildren();
	}

	// Places every child relative to `bodyOffset` (0 unless a pointer has grown the box
	// up/left), from the measurements layout() cached — so setPointer() can reposition
	// everything after a box resize without re-running text measurement.
	void positionChildren() {
		float pad = style.padding;
		float innerWidth = measuredWidth - 2.f * pad;
		float y = pad;

		float closeSize = style.closeButtonSize;
		captionLabel->box.pos = bodyOffset.plus(math::Vec(pad, y));
		captionLabel->box.size = math::Vec(innerWidth - closeSize - pad * 0.5f, measuredCaptionH);

		closeButton->box.pos = bodyOffset.plus(math::Vec(pad + innerWidth - closeSize, y - 1.f));
		closeButton->box.size = math::Vec(closeSize, closeSize);
		y += measuredCaptionH + pad * 0.5f;

		titleLabel->box.pos = bodyOffset.plus(math::Vec(pad, y));
		titleLabel->box.size = math::Vec(innerWidth, measuredTitleH);
		y += measuredTitleH + pad * 0.5f;

		bodyLabel->box.pos = bodyOffset.plus(math::Vec(pad, y));
		bodyLabel->box.size = math::Vec(innerWidth, measuredBodyH);
		y += measuredBodyH + pad;

		float buttonH = style.buttonHeight;
		float buttonW = (innerWidth - pad) * 0.5f;
		if (backButton->visible) {
			backButton->box.pos = bodyOffset.plus(math::Vec(pad, y));
			backButton->box.size = math::Vec(buttonW, buttonH);
			nextButton->box.pos = bodyOffset.plus(math::Vec(pad + buttonW + pad, y));
			nextButton->box.size = math::Vec(buttonW, buttonH);
		}
		else {
			// Next/Finish alone, right-aligned to match the two-button row's right edge.
			nextButton->box.pos = bodyOffset.plus(math::Vec(pad + innerWidth - buttonW, y));
			nextButton->box.size = math::Vec(buttonW, buttonH);
		}
	}

public:
	void draw(const DrawArgs& args) override {}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 3) return;

		nvgSave(args.vg);
		nvgGlobalTint(args.vg, color::WHITE);
		nvgGlobalCompositeOperation(args.vg, NVG_SOURCE_OVER);

		drawOutline(args.vg);

		Widget::draw(args);

		nvgRestore(args.vg);
	}

private:
	// One path: the bubble's rounded rect, with the pointer triangle spliced into whichever
	// straight edge `pointerSide` names. Corner rounding uses the same
	// bezier-kappa construction as nanovg's own nvgRoundedRectVarying, since nvgArcTo's
	// corner-only form can't be interrupted mid-edge to splice the triangle in.
	void drawOutline(NVGcontext* vg) {
		static constexpr float KAPPA = 0.5522847493f;

		math::Rect body(bodyOffset, bodySize);
		float r = style.cornerRadius;
		float x = body.pos.x, y = body.pos.y, w = body.size.x, h = body.size.y;

		// Drop shadow, matching Ahab's info dialog (InfoWindow.hpp draws through
		// bndMenuBackground(), which calls this same helper) — this bubble draws its own
		// outline instead of going through bndMenuBackground() (it needs the pointer notch
		// spliced into the path, and layer 3 instead of layer 1), so the shadow needs adding
		// separately. Feather scaled down from Blendish's own BND_SHADOW_FEATHER (12px, sized
		// for full BND_WIDGET_HEIGHT controls) to suit this bubble's much smaller scale.
		bndDropShadow(vg, x, y, w, h, r, 6.f, 0.5f);

		nvgBeginPath(vg);
		nvgMoveTo(vg, x, y + r);

		// Left edge, top-left corner already placed by moveTo.
		drawEdge(vg, Side::LEFT, math::Vec(x, y + r), math::Vec(x, y + h - r));
		nvgBezierTo(vg, x, y + h - r * (1 - KAPPA), x + r * (1 - KAPPA), y + h, x + r, y + h);

		// Bottom edge.
		drawEdge(vg, Side::BOTTOM, math::Vec(x + r, y + h), math::Vec(x + w - r, y + h));
		nvgBezierTo(vg, x + w - r * (1 - KAPPA), y + h, x + w, y + h - r * (1 - KAPPA), x + w, y + h - r);

		// Right edge.
		drawEdge(vg, Side::RIGHT, math::Vec(x + w, y + h - r), math::Vec(x + w, y + r));
		nvgBezierTo(vg, x + w, y + r * (1 - KAPPA), x + w - r * (1 - KAPPA), y, x + w - r, y);

		// Top edge.
		drawEdge(vg, Side::TOP, math::Vec(x + w - r, y), math::Vec(x + r, y));
		nvgBezierTo(vg, x + r * (1 - KAPPA), y, x, y + r * (1 - KAPPA), x, y + r);

		nvgClosePath(vg);

		nvgFillColor(vg, bndGetTheme()->menuTheme.innerColor);
		nvgFill(vg);
		nvgStrokeColor(vg, bndGetTheme()->menuTheme.outlineColor);
		nvgStrokeWidth(vg, 1.f);
		nvgStroke(vg);
	}

	// Draws the straight segment from `from` to `to`; if `pointerSide == side`, the triangle
	// (baseA -> tip -> baseB) is spliced in first, in edge-walk order.
	void drawEdge(NVGcontext* vg, Side side, math::Vec from, math::Vec to) {
		if (!hasPointer || pointerSide != side) {
			nvgLineTo(vg, to.x, to.y);
			return;
		}
		// baseA/baseB were computed in placement order (A before B along the edge); the edge
		// walk direction here may run either forward or backward relative to that, so pick
		// whichever of the two is closer to `from` as the first vertex reached.
		bool aFirst = pointerBaseA.minus(from).square() <= pointerBaseB.minus(from).square();
		math::Vec first = aFirst ? pointerBaseA : pointerBaseB;
		math::Vec second = aFirst ? pointerBaseB : pointerBaseA;

		nvgLineTo(vg, first.x, first.y);
		nvgLineTo(vg, pointerTip.x, pointerTip.y);
		nvgLineTo(vg, second.x, second.y);
		nvgLineTo(vg, to.x, to.y);
	}
};


// The per-module tutorial state: dims the panel except a cutout around the current step's
// target, owns the TutorialBubble living in the rack, and drives the placement engine every
// step(). Child of the ModuleWidget, added last, so it draws (layer 1) after the module's own
// LEDs and sits in front of the panel's controls for events.
struct TutorialOverlay : widget::OpaqueWidget, TutorialBubbleHost {
	Tutorial tutorial;
	int index = 0;
	Side lastSide = Side::AUTO;
	TutorialBubble* bubble = nullptr;

	// Accent colour and pulse rate for the highlight ring. Not in Style (Style is sizes/spacing
	// only); tuned by eye like everything else in that struct.
	static NVGcolor accentColor() { return nvgRGB(0xf2, 0xa5, 0x3a); }
	static constexpr float RING_WIDTH = 2.f;
	static constexpr float PULSE_HZ = 0.5f;
	static constexpr float DIM_ALPHA = 0.55f;

	app::ModuleWidget* moduleWidget() const {
		return dynamic_cast<app::ModuleWidget*>(parent);
	}

	// The step's target rect in module-local coordinates, including highlight padding, and
	// whether one resolved at all (false -> centered step).
	bool resolveTarget(app::ModuleWidget* mw, math::Rect& out) const {
		if (index < 0 || index >= (int) tutorial.steps.size()) return false;
		const Step& step = tutorial.steps[index];
		if (!step.target.resolve) return false;
		math::Rect r;
		if (!step.target.resolve(mw, r)) {
			WARN("Tutorial step %d: target failed to resolve, falling back to centered", index);
			return false;
		}
		out = r.grow(math::Vec(bubble->style.highlightPadding, bubble->style.highlightPadding));
		return true;
	}

	// The on-screen part of the rack viewport, in module-local coordinates.
	math::Rect visibleRegion(app::ModuleWidget* mw) const {
		vcv::RackViewport vp = vcv::ui::getRackViewport();
		math::Vec tl = mw->getRelativeOffset(math::Vec(), APP->scene);
		float zoom = vp.zoom > 0.f ? vp.zoom : 1.f;
		math::Rect visible(vp.box.pos.minus(tl).div(zoom), vp.box.size.div(zoom));
		return visible.shrink(math::Vec(bubble->style.boundsMargin, bubble->style.boundsMargin));
	}

	void step() override {
		app::ModuleWidget* mw = moduleWidget();
		if (!mw) {
			OpaqueWidget::step();
			return;
		}
		box = mw->box.zeroPos();

		// Lazy one-time setup, once this is actually parented and the rack exists.
		if (!bubble && APP->scene && APP->scene->rack) {
			bubble = new TutorialBubble;
			bubble->setHost(this);
			APP->scene->rack->addChild(bubble);
			APP->event->setSelectedWidget(this);
			if (!tutorial.steps.empty() && tutorial.steps[0].onEnter) tutorial.steps[0].onEnter();
		}

		if (bubble && index >= 0 && index < (int) tutorial.steps.size()) {
			const Step& current = tutorial.steps[index];

			math::Rect targetRect;
			bool hasTarget = resolveTarget(mw, targetRect);
			targetRectModule = hasTarget ? targetRect : math::Rect();
			hasTargetRect = hasTarget;

			char caption[64];
			std::snprintf(caption, sizeof(caption), "%s \xC2\xB7 %d / %d", tutorial.title.c_str(),
				index + 1, (int) tutorial.steps.size());
			float width = current.width > 0.f ? current.width : bubble->style.bubbleWidth;
			bool showBack = index > 0;
			bool isLast = index == (int) tutorial.steps.size() - 1;
			bubble->setContent(index, caption, current.title, current.text, showBack, isLast, width);

			PlacementInput in;
			in.bounds = visibleRegion(mw);
			in.module = mw->box.zeroPos();
			in.hasTarget = hasTarget;
			in.target = targetRect;
			in.bubble = bubble->getBodySize();
			in.preferred = current.side;
			in.previous = lastSide;
			in.style = bubble->style;

			Placement placement = place(in);
			if (placement.side != Side::AUTO) lastSide = placement.side;

			// Map the bubble's body position and pointer geometry from module-local into
			// rack-local coordinates: both mw and the bubble are (indirect) children of the same
			// RackWidget, so stopping getRelativeOffset there gives coordinates the bubble's own
			// box.pos (a rack child) can use directly.
			//
			// placement.box.pos is the BODY's top-left, not the bubble widget's box.pos —
			// setPointer() may grow the box left/up to fit the pointer triangle, shifting the
			// body within it (TutorialBubble::getBodyOffset()). Pointer coordinates are pushed
			// in relative to that same body top-left, so they must be computed before
			// bodyOffset shifts the box, and box.pos set after.
			math::Vec bodyRackTopLeft = mw->getRelativeOffset(placement.box.pos, APP->scene->rack);

			if (placement.hasPointer) {
				math::Vec tip = mw->getRelativeOffset(placement.pointerTip, APP->scene->rack).minus(bodyRackTopLeft);
				math::Vec baseA = mw->getRelativeOffset(placement.pointerBaseA, APP->scene->rack).minus(bodyRackTopLeft);
				math::Vec baseB = mw->getRelativeOffset(placement.pointerBaseB, APP->scene->rack).minus(bodyRackTopLeft);
				// placement.side is which side of the TARGET the bubble sits on; the notch goes
				// on the opposite edge of the BUBBLE (placed RIGHT of the target -> the bubble's
				// own LEFT edge points back at it). setPointer()'s `side` means the latter.
				bubble->setPointer(true, oppositeSide(placement.side), tip, baseA, baseB);
			}
			else {
				bubble->setPointer(false, Side::AUTO, math::Vec(), math::Vec(), math::Vec());
			}
			bubble->box.pos = bodyRackTopLeft.minus(bubble->getBodyOffset());
		}

		OpaqueWidget::step();
	}

	// Cached from the last step(), for events that need the target rect without re-resolving
	// (right now: none, kept for the drawLayer(1) cutout below to avoid a second resolve call
	// mid-frame with a possibly-different result than what step() placed against).
	math::Rect targetRectModule;
	bool hasTargetRect = false;

	void draw(const DrawArgs& args) override {}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1) return;
		if (!bubble) return;

		nvgSave(args.vg);
		nvgGlobalCompositeOperation(args.vg, NVG_SOURCE_OVER);

		nvgBeginPath(args.vg);
		nvgRect(args.vg, 0.f, 0.f, box.size.x, box.size.y);
		if (hasTargetRect) {
			nvgRoundedRect(args.vg, RECT_ARGS(targetRectModule), bubble->style.cornerRadius);
			nvgPathWinding(args.vg, NVG_HOLE);
		}
		nvgFillColor(args.vg, nvgRGBAf(0.f, 0.f, 0.f, DIM_ALPHA));
		nvgFill(args.vg);

		if (hasTargetRect) {
			float t = (float) vcv::fs::getTime();
			float alpha = 0.5f + 0.5f * std::sin(2.f * (float) M_PI * PULSE_HZ * t);
			nvgBeginPath(args.vg);
			nvgRoundedRect(args.vg, RECT_ARGS(targetRectModule), bubble->style.cornerRadius);
			NVGcolor c = accentColor();
			nvgStrokeColor(args.vg, nvgRGBAf(c.r, c.g, c.b, 0.5f + 0.5f * alpha));
			nvgStrokeWidth(args.vg, RING_WIDTH);
			nvgStroke(args.vg);
		}

		nvgRestore(args.vg);

		OpaqueWidget::drawLayer(args, layer);
	}

	// ---- Events -----------------------------------------------------------------------------

	void onButton(const ButtonEvent& e) override {
		Widget::onButton(e);
		e.stopPropagating();
		// Deliberately never consumed: left-click still lets ModuleWidget::onButton drag the
		// module / open its context menu on right-click. Only propagation to the panel's own
		// children (knobs, ports, ...) is blocked.
	}

	void onHover(const HoverEvent& e) override {
		Widget::onHover(e);
		e.stopPropagating();
		e.consume(this);
	}

	void onHoverScroll(const HoverScrollEvent& e) override {
		Widget::onHoverScroll(e);
		e.stopPropagating();
		// Not consumed: RackScrollWidget (an ancestor, dispatched to separately) still
		// scrolls/zooms. Only the panel's own knobs are blocked from seeing it.
	}

	bool isNavigationKey(int key) const {
		return key == GLFW_KEY_LEFT || key == GLFW_KEY_RIGHT || key == GLFW_KEY_ENTER || key == GLFW_KEY_ESCAPE;
	}

	void handleNavigationKey(const KeyBaseEvent& e) {
		if (e.action != GLFW_PRESS) return;
		if (e.key == GLFW_KEY_RIGHT || e.key == GLFW_KEY_ENTER) next();
		else if (e.key == GLFW_KEY_LEFT) back();
		else if (e.key == GLFW_KEY_ESCAPE) close();
	}

	void onHoverKey(const HoverKeyEvent& e) override {
		Widget::onHoverKey(e);
		e.stopPropagating();
		if (isNavigationKey(e.key)) {
			handleNavigationKey(e);
			e.consume(this);
		}
	}

	void onSelectKey(const SelectKeyEvent& e) override {
		if (isNavigationKey(e.key)) {
			handleNavigationKey(e);
			e.consume(this);
		}
	}

	// ---- TutorialBubbleHost -----------------------------------------------------------------

	void fireLeave() {
		if (index >= 0 && index < (int) tutorial.steps.size() && tutorial.steps[index].onLeave) {
			tutorial.steps[index].onLeave();
		}
	}

	void fireEnter() {
		if (index >= 0 && index < (int) tutorial.steps.size() && tutorial.steps[index].onEnter) {
			tutorial.steps[index].onEnter();
		}
	}

	void next() override {
		if (index >= (int) tutorial.steps.size() - 1) {
			close();
			return;
		}
		fireLeave();
		index++;
		fireEnter();
	}

	void back() override {
		if (index <= 0) return;
		fireLeave();
		index--;
		fireEnter();
	}

	void close() override {
		fireLeave();
		requestDelete();
	}

	~TutorialOverlay() {
		if (bubble) {
			APP->scene->rack->removeChild(bubble);
			delete bubble;
			bubble = nullptr;
		}
	}
};


} // namespace Tutorial
} // namespace StoermelderPackOne