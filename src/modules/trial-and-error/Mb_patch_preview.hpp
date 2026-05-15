#pragma once
#include "../../plugin.hpp"
#include "../strip/vcvs_helpers.hpp"
#include "Mb_patch.hpp"

namespace StoermelderPackOne {
namespace Mb {
namespace patch {

	
struct PreviewWidget : OpaqueWidget {
	Browser* browser = nullptr;
	std::string fileId;
	std::string lastFileId;
	json_t* rootJ = nullptr;

	float modelOpacity = 1.f;
	math::Vec lastBoxSize;

	// Cached content dimensions for resize handling
	float contentWidth = 0.f;
	float contentHeight = 0.f;
	bool contentCached = false;

	/** True once fitPreviewToBox() has completed for the current patch. */
	bool fitted = false;

	// Center offset for scaled content
	float scaledContentOffsetX = 0.f;
	float scaledContentOffsetY = 0.f;

	/**
	 * Loads a patch and takes ownership of rootJ
	 */
	bool setPatch(std::string fileId, json_t* rootJ);

	/**
	 * Clears the current patch and deletes the preview, clears fileId and deconstructs rootJ.
	 */
	void clearPatch();

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


} // namespace patch
} // namespace Mb
} // namespace StoermelderPackOne