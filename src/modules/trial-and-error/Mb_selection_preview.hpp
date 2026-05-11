#pragma once
#include "../../plugin.hpp"
#include "../strip/vcvs_helpers.hpp"
#include "Mb_selection.hpp"

namespace StoermelderPackOne {
namespace Mb {
namespace selection {

	
struct SelectionPreviewWidget : OpaqueWidget {
	SelectionBrowser* browser = nullptr;
	std::string fileId;
	std::string lastFileId;
	json_t* rootJ = nullptr;

	float modelOpacity = 1.f;
	math::Vec lastBoxSize;

	// Cached content dimensions for resize handling
	float contentWidth = 0.f;
	float contentHeight = 0.f;
	bool contentCached = false;

	// Center offset for scaled content
	float scaledContentOffsetX = 0.f;
	float scaledContentOffsetY = 0.f;

	/**
	 * Loads a selection and takes ownership of rootJ
	 */
	bool setSelection(std::string fileId, json_t* rootJ);

	/**
	 * Clears the current selection and deletes the preview, clears fileId and deconstructs rootJ.
	 */
	void clearSelection();

	/**
	 * Internal function. Fits the preview to the current box size by calculating a uniform
	 * scale factor and centering offset, then applying those to all ModelPreviewWidget children.
	 */
	void fitPreviewToBox();

	/**
	 * Internal function. Resets cached box size to force fitPreviewToBox() to run on next step,
	 * which is necessary after creating a new preview or changing the fileId.
	 */
	void refreshPreview();

	/**
	 * Internal function. Creates a new preview based on the current fileId and rootJ, 
	 * replacing any existing preview widgets.
	 */
	void createPreview();
	
	void onButton(const event::Button& e) override;
	void step() override;
	void draw(const DrawArgs& args) override;
	void createContextMenu();
};


} // namespace selection
} // namespace Mb
} // namespace StoermelderPackOne