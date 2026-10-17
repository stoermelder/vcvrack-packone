#pragma once
extern "C" {
	#include <orca-c/base.h>
}

namespace StoermelderPackOne {
namespace Ahab {

static const Mark Mark_flag_uninit = (1 << 7);

// Maximum field dimensions. The field is capped at this size so the fixed
// DSP-thread scratch buffer (AhabSim::scratch_) is always large enough, and no
// heap buffer is ever shipped across the UI->DSP command queue. The editor
// state and the clipboard are sized from these, too.
static constexpr Usz MAX_FIELD_HEIGHT = 100;
static constexpr Usz MAX_FIELD_WIDTH = 100;

// AhabSharedEditorState packs every coordinate and size into 7 bits.
static_assert(MAX_FIELD_HEIGHT <= 127 && MAX_FIELD_WIDTH <= 127, "editor state packs 7 bits per coordinate");

} // namespace Ahab
} // namespace StoermelderPackOne