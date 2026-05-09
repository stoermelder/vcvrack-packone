#pragma once
#include "GlueTypes.hpp"

namespace StoermelderPackOne {
namespace Glue {

struct ModuleLabelDrawWidget : TransparentWidget {
	ModuleLabel* label;
	Vec rotatedSize;

	void draw(const Widget::DrawArgs& args) override;
};


struct ModuleLabelWidget : widget::TransparentWidget {
	ModuleLabel* label;

	bool requestedDelete = false;
	bool requestedDuplicate = false;
	bool editMode = false;
	bool skew = false;

	math::Vec dragPos;

	ModuleLabelDrawWidget* widget;
	TransformWidget* transformWidget;
	float lastAngle = 360.f;
	float lastSize = 0.f;
	float lastWidth = 0.f;
	bool lastSkew = false;

	ModuleLabelWidget(ModuleLabel* label);

	void step() override;
	void onButton(const event::Button& e) override;
	void onDragStart(const event::DragStart& e) override;
	void onDragMove(const event::DragMove& e) override;
	void createContextMenu();
	void onHoverKey(const event::HoverKey& e) override;
};

} // namespace Glue
} // namespace StoermelderPackOne
