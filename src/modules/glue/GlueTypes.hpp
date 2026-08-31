#pragma once
#include "../../plugin.hpp"
#include "../../utils/string.hpp"

namespace StoermelderPackOne {
namespace Glue {

using namespace strings;

// Color constants
const static NVGcolor LABEL_COLOR_YELLOW = nvgRGB(0xdc, 0xff, 0x46);
const static NVGcolor LABEL_COLOR_RED = nvgRGB(0xff, 0x74, 0x55);
const static NVGcolor LABEL_COLOR_CYAN = nvgRGB(0x7a, 0xfc, 0xff);
const static NVGcolor LABEL_COLOR_GREEN = nvgRGB(0x1b, 0xa8, 0xb1);
const static NVGcolor LABEL_COLOR_PINK = nvgRGB(0xff, 0x65, 0xa3);
const static NVGcolor LABEL_COLOR_WHITE = nvgRGB(0xfa, 0xfa, 0xfa);

const static NVGcolor LABEL_FONTCOLOR_DEFAULT = nvgRGB(0x08, 0x08, 0x08);
const static NVGcolor LABEL_FONTCOLOR_WHITE = nvgRGB(0xf8, 0xf8, 0xf8);

// Label size constants
const static float LABEL_OPACITY_MAX = 1.0f;
const static float LABEL_OPACITY_MIN = 0.2f;
const static float LABEL_OPACITY_STEP = 0.05f;

const static float LABEL_WIDTH_MAX = 360.f;
const static float LABEL_WIDTH_MIN = 20.f;
const static float LABEL_WIDTH_DEFAULT = 80.f;

const static float LABEL_SIZE_MAX = 48.f;
const static float LABEL_SIZE_MIN = 8.f;
const static float LABEL_SIZE_DEFAULT = 16.f;

const static float LABEL_SKEW_MAX = 3.5f;


// Label data structures
struct ModuleLabel {
	int64_t moduleId;
	float x = 0.f;
	float y = 0.f;
	float width = LABEL_WIDTH_DEFAULT;
	float size = LABEL_SIZE_DEFAULT;
	float angle = 0.f;
	float skew = 0.f;
	float opacity = 1.f;
	int font = 0;
	std::string text;
	NVGcolor color = LABEL_COLOR_YELLOW;
	NVGcolor fontColor = LABEL_FONTCOLOR_DEFAULT;
};


struct CableLabel {
	int64_t cableId;
	bool atInput = true; // Whether label is at input port (true) or output port (false)
	float width = LABEL_WIDTH_DEFAULT;
	float size = LABEL_SIZE_DEFAULT;
	float distance = 40.f; // Distance from port along cable
	int font = 0;
	std::string text;
	NVGcolor color = LABEL_COLOR_YELLOW; // Auto-set from cable, but can be overridden
	NVGcolor fontColor = LABEL_FONTCOLOR_DEFAULT; // Auto-set for contrast
	
	// Transient port references for tracking during incomplete cable state (not stored to JSON)
	PortWidget* lastOutputPort = NULL;
	PortWidget* lastInputPort = NULL;
	
	// Cache for placement calculations (only computed/derived values)
	Vec cachedOutputPos = Vec(-1.f, -1.f);
	Vec cachedInputPos = Vec(-1.f, -1.f);
	Vec cachedBoxPos;
	Vec cachedBoxSize;
	float cachedLabelAngle = 0.f;
	Vec cachedRotatedSize;
	bool cacheValid = false;
};

} // namespace Glue
} // namespace StoermelderPackOne
