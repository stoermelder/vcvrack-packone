#include "CableLabelWidget.hpp"
#include "../../plugin.hpp"

namespace StoermelderPackOne {
namespace Glue {

void CableLabelDrawWidget::drawLayer(const Widget::DrawArgs& args, int layer) {
	if (layer != 3) return;
	if (!cableLabel) return;

	Rect d = Rect(Vec(0.f, 0.f), rotatedSize);

	// Draw shadow
	nvgBeginPath(args.vg);
	float r = 4;
	float c = 4;
	math::Vec b = math::Vec(-2.f, -2.f);
	nvgRect(args.vg, d.pos.x + b.x - r, d.pos.y + b.y - r, d.size.x - 2 * b.x + 2 * r, d.size.y - 2 * b.y + 2 * r);
	NVGcolor shadowColor = nvgRGBAf(0.f, 0.f, 0.f, 0.1f);
	NVGcolor transparentColor = nvgRGBAf(0.f, 0.f, 0.f, 0.f);
	nvgFillPaint(args.vg, nvgBoxGradient(args.vg, d.pos.x + b.x, d.pos.y + b.y, d.size.x - 2 * b.x, d.size.y - 2 * b.y, c, r, shadowColor, transparentColor));
	nvgFill(args.vg);

	// Draw label
	nvgBeginPath(args.vg);
	nvgRect(args.vg, d.pos.x, d.pos.y, d.size.x, d.size.y);
	nvgFillColor(args.vg, color::alpha(cableLabel->color, settings::cableOpacity));
	nvgFill(args.vg);

	// Draw text
	if (cableLabel->text.length() > 0) {
		std::shared_ptr<Font> font;
		switch (cableLabel->font) {
			case 0:
				font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
				break;
			case 1:
				font = APP->window->loadFont(asset::plugin(pluginInstance, "res/fonts/RedkostComic.otf"));
				break;
		}

		nvgFontSize(args.vg, cableLabel->size);
		nvgFontFaceId(args.vg, font->handle);
		nvgTextLetterSpacing(args.vg, -1.2f);
		nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
		nvgFillColor(args.vg, color::alpha(cableLabel->fontColor, settings::cableOpacity));
		NVGtextRow textRow;
		nvgTextBreakLines(args.vg, cableLabel->text.c_str(), NULL, d.size.x, &textRow, 1);
		nvgTextBox(args.vg, d.pos.x, d.pos.y + 0.2f, d.size.x, textRow.start, textRow.end);
	}
}


CableLabelWidget::CableLabelWidget(CableLabel* cableLabel) {
	this->cableLabel = cableLabel;

	widget = new CableLabelDrawWidget;
	widget->cableLabel = cableLabel;
	transformWidget = new TransformWidget;
	transformWidget->addChild(widget);
	addChild(transformWidget);
}

void CableLabelWidget::step() {
	// Find the cable in the rack - search by cable ID
	CableWidget* cw = NULL;
	for (Widget* w : APP->scene->rack->getCableContainer()->children) {
		CableWidget* cwTest = dynamic_cast<CableWidget*>(w);
		if (cwTest) {
			// Match by cable ID if cable exists, or by stored ports if incomplete
			if (cwTest->cable && cwTest->cable->id == cableLabel->cableId) {
				cw = cwTest;
				break;
			}
		}
	}

	// If cable not found, it might be incomplete - search by matching ports
	if (!cw) {
		for (Widget* w : APP->scene->rack->getCableContainer()->children) {
			CableWidget* cwTest = dynamic_cast<CableWidget*>(w);
			if (cwTest && !cwTest->cable) {
				// Check if this incomplete cable matches our stored cable ID context
				// by checking if it has the same ports as our labeled cable
				if (cableLabel->lastOutputPort && cableLabel->lastInputPort) {
					if (cwTest->outputPort == cableLabel->lastOutputPort || 
						cwTest->inputPort == cableLabel->lastInputPort) {
						cw = cwTest;
						break;
					}
				}
			}
		}
	}

	// Request deletion only if cable truly doesn't exist anymore
	if (!cw) {
		// Check if the cable still exists in engine
		bool cableExistsInEngine = false;
		for (Widget* w : APP->scene->rack->getCableContainer()->children) {
			CableWidget* cwTest = dynamic_cast<CableWidget*>(w);
			if (cwTest && cwTest->cable && cwTest->cable->id == cableLabel->cableId) {
				cableExistsInEngine = true;
				break;
			}
		}
		if (!cableExistsInEngine) {
			requestedDelete = true;
		}
		visible = false;
		return;
	}

	// Store port references for tracking during incomplete state
	if (cw->isComplete()) {
		cableLabel->lastOutputPort = cw->outputPort;
		cableLabel->lastInputPort = cw->inputPort;
	}

	// Show label even if cable is incomplete (being dragged), as long as we can position it
	if (!cw->cable || !cw->isComplete()) {
		// Only show if at least one port is connected so we can calculate position
		if (!cw->outputPort && !cw->inputPort) {
			visible = false;
			return;
		}
		// For incomplete cables, we'll show the label where it would be
	}
	visible = true;

	// Get positions
	Vec outputPos = cw->getOutputPos();
	Vec inputPos = cw->getInputPos();
	
	// Check if we can use cached calculations
	if (cableLabel->cacheValid && 
		cableLabel->cachedOutputPos.equals(outputPos) && 
	    cableLabel->cachedInputPos.equals(inputPos)) {
		// Use cached values
		box.pos = cableLabel->cachedBoxPos;
		box.size = cableLabel->cachedBoxSize;
		widget->rotatedSize = cableLabel->cachedRotatedSize;
		widget->box.size = box.size;
		
		// Set transform from cached angle
		transformWidget->identity();
		transformWidget->translate(Vec(box.size.x / 2.f, box.size.y / 2.f));
		transformWidget->rotate(cableLabel->cachedLabelAngle);
		transformWidget->translate(Vec(-cableLabel->width / 2.f, -cableLabel->size / 2.f));
		
		// Still need to update colors
		cableLabel->color = cw->color;
		float brightness = (cw->color.r * 0.299f + cw->color.g * 0.587f + cw->color.b * 0.114f);
		cableLabel->fontColor = brightness > 0.5f ? LABEL_FONTCOLOR_DEFAULT : LABEL_FONTCOLOR_WHITE;
		return;
	}
	
	// Cache miss - need to recalculate
	cableLabel->cachedOutputPos = outputPos;
	cableLabel->cachedInputPos = inputPos;
	
	// Calculate slump position (matching VCV Rack's getSlumpPos function)
	// This is exactly how VCV Rack calculates the cable curve control point
	float dist = outputPos.minus(inputPos).norm();
	Vec slump = outputPos.plus(inputPos).div(2.f);
	slump.y += (1.0f - settings::cableTension) * (150.0f + 1.0f * dist);
	
	// Adjust endpoints toward slump (matching VCV Rack's cable drawing)
	outputPos = outputPos.plus(slump.minus(outputPos).normalize().mult(14.f));
	inputPos = inputPos.plus(slump.minus(inputPos).normalize().mult(14.f));
	
	// Calculate position at configurable distance from port along the cable curve
	// Use iterative approach to find t value that gives desired distance
	float targetDist = cableLabel->distance; // Distance from port in pixels
	
	// Binary search for t value that gives target distance from the appropriate port
	// For output-side labels, measure from output; for input-side labels, measure from input
	float t = 0.f;
	float tMin = 0.f;
	float tMax = 0.5f; // Only search first half of cable
	Vec referencePort = cableLabel->atInput ? inputPos : outputPos;
	
	for (int i = 0; i < 10; i++) {
		t = (tMin + tMax) / 2.f;
		float tTest = cableLabel->atInput ? (1.f - t) : t;
		float oneMinusT = 1.f - tTest;
		
		// Calculate position at t
		Vec pos = outputPos.mult(oneMinusT * oneMinusT)
			.plus(slump.mult(2.f * oneMinusT * tTest))
			.plus(inputPos.mult(tTest * tTest));
		
		// Measure distance from the reference port
		float currentDist = pos.minus(referencePort).norm();
		if (currentDist < targetDist) {
			tMin = t;
		}
		else {
			tMax = t;
		}
	}
	
	// Use the found t value (already adjusted for input/output in the search)
	float tFinal = cableLabel->atInput ? (1.f - t) : t;
	float oneMinusT = 1.f - tFinal;
	
	Vec labelCenter = outputPos.mult(oneMinusT * oneMinusT)
		.plus(slump.mult(2.f * oneMinusT * tFinal))
		.plus(inputPos.mult(tFinal * tFinal));
	
	// Calculate tangent vector (derivative of quadratic Bezier)
	// B'(t) = 2(1-t)(P₁-P₀) + 2t(P₂-P₁)
	Vec tangent = slump.minus(outputPos).mult(2.f * oneMinusT)
		.plus(inputPos.minus(slump).mult(2.f * tFinal));
	
	// Calculate angle along the cable
	float tangentAngle = std::atan2(tangent.y, tangent.x);
	
	// Rotate label 90° from tangent so short side (height) aligns with cable
	// Keep text readable (never upside down)
	float labelAngle = tangentAngle + M_PI / 2.f;
	if (labelAngle < -M_PI / 2.f) labelAngle += M_PI;
	if (labelAngle > M_PI / 2.f) labelAngle -= M_PI;
	
	// Set cable color (from cable widget)
	cableLabel->color = cw->color;
	
	// Calculate contrasting font color based on cable color brightness
	float brightness = (cw->color.r * 0.299f + cw->color.g * 0.587f + cw->color.b * 0.114f);
	cableLabel->fontColor = brightness > 0.5f ? LABEL_FONTCOLOR_DEFAULT : LABEL_FONTCOLOR_WHITE;
	
	// Calculate perpendicular offset from cable, always pointing downward
	float offsetDist = cableLabel->width / 2.f + 1.f; // Half the label's perpendicular extent + gap
	// Perpendicular to tangent is at tangentAngle ± π/2
	// Choose the perpendicular that points downward (positive Y)
	float perpAngle = tangentAngle + M_PI / 2.f;
	Vec perpDir = Vec(std::cos(perpAngle), std::sin(perpAngle));
	// If this perpendicular points upward, flip it
	if (perpDir.y < 0.f) {
		perpAngle += M_PI;
		perpDir = Vec(std::cos(perpAngle), std::sin(perpAngle));
	}
	Vec perpOffset = perpDir.mult(offsetDist);
	
	// Position label - box size is swapped due to 90° rotation
	box.size = Vec(cableLabel->size, cableLabel->width);
	box.pos = labelCenter.plus(perpOffset).minus(Vec(cableLabel->size / 2.f, cableLabel->width / 2.f));

	widget->rotatedSize = Vec(cableLabel->width, cableLabel->size);
	widget->box.size = box.size;

	// Rotate label 90° from tangent
	transformWidget->identity();
	transformWidget->translate(Vec(box.size.x / 2.f, box.size.y / 2.f));
	transformWidget->rotate(labelAngle);
	transformWidget->translate(Vec(-cableLabel->width / 2.f, -cableLabel->size / 2.f));
	
	// Store in cache
	cableLabel->cachedBoxPos = box.pos;
	cableLabel->cachedBoxSize = box.size;
	cableLabel->cachedLabelAngle = labelAngle;
	cableLabel->cachedRotatedSize = widget->rotatedSize;
	cableLabel->cacheValid = true;

	TransparentWidget::step();
}

void CableLabelWidget::onHoverKey(const event::HoverKey& e) {
	if (editMode && e.action == GLFW_PRESS && (e.mods & RACK_MOD_MASK) == RACK_MOD_CTRL && e.key == GLFW_KEY_X) {
		requestedDelete = true;
		e.consume(this);
	}
	TransparentWidget::onHoverKey(e);
}

} // namespace Glue
} // namespace StoermelderPackOne
