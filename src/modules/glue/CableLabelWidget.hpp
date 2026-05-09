#pragma once
#include "GlueTypes.hpp"

namespace StoermelderPackOne {
namespace Glue {

struct CableLabelDrawWidget : TransparentWidget {
	CableLabel* cableLabel;
	Vec rotatedSize;

	void drawLayer(const Widget::DrawArgs& args, int layer) override;
};


struct CableLabelWidget : widget::TransparentWidget {
	CableLabel* cableLabel;

	bool requestedDelete = false;
	bool requestedDuplicate = false;
	bool editMode = false;
	bool skew = false;

	CableLabelDrawWidget* widget;
	TransformWidget* transformWidget;
	float lastAngle = 360.f;
	float lastSize = 0.f;
	float lastWidth = 0.f;
	bool lastSkew = false;

	CableLabelWidget(CableLabel* cableLabel);

	void step() override;
	void onHoverKey(const event::HoverKey& e) override;
};

} // namespace Glue
} // namespace StoermelderPackOne
