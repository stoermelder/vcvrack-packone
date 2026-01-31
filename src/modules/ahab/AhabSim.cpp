#include "AhabSim.hpp"
#include "Ahab.hpp"
#include <string.hpp>
#include <cstdio>
#include <unordered_map>
#include <cmath>
#include <cctype>
#include <sstream>
#include <algorithm>

extern "C" {
	#include "../../../dep/orca-c/osc_out.h"
}

namespace StoermelderPackOne {
namespace Ahab {

static inline void trimStr(std::string &s) {
	// left
	size_t i = 0;
	while (i < s.size() && std::isspace((unsigned char)s[i])) ++i;
	s.erase(0, i);
	// right
	if (!s.empty()) {
		size_t j = s.size() - 1;
		while (j != (size_t)-1 && std::isspace((unsigned char)s[j])) --j;
		s.erase(j + 1);
	}
}

static inline bool isValidPort(const std::string &s) {
	if (s.empty()) return true;
	for (char c : s) if (!std::isdigit((unsigned char)c)) return false;
	long p = 0;
	try { p = std::stol(s); } catch (...) { return false; }
	return (p >= 1 && p <= 65535);
}

static inline bool containsWhitespace(const std::string &s) {
	for (char c : s) if (std::isspace((unsigned char)c)) return true;
	return false;
}

static void mbuf_uninit_mark(Mark* mbr, Usz height, Usz width) {
	for (Usz y = 0; y < height; ++y) {
		for (Usz x = 0; x < width; ++x) {
			mbr[y * width + x] = Mark_flag_uninit;
		}
	}
}


// Parse ORCA plain text into a Field object. The ORCA text format is rows of characters
// separated by '\n'. Trailing newline that produces an empty last line is ignored.
bool AhabSim::buildFieldFromOrcaText(const std::string& orcaText, Field& out) {
	std::vector<std::string> lines;
	{
		std::string cur;
		for (char ch : orcaText) {
			if (ch == '\r') continue; // ignore CR
			if (ch == '\n') {
				lines.push_back(cur);
				cur.clear();
			} else {
				cur.push_back(ch);
			}
		}
		if (!cur.empty()) lines.push_back(cur);
	}
	// If text ended with a newline there is an empty trailing line - ignore it
	while (!lines.empty() && lines.back().empty()) lines.pop_back();
	if (lines.empty()) return false;
	size_t h = lines.size();
	size_t w = 0;
	for (auto &l : lines) if (l.size() > w) w = l.size();
	if (w == 0) return false;
	field_init_fill(&out, (Usz)h, (Usz)w, '.');
	for (size_t ry = 0; ry < h; ++ry) {
		const std::string &row = lines[ry];
		for (size_t cx = 0; cx < row.size(); ++cx) {
			out.buffer[ry * w + cx] = (Glyph)row[cx];
		}
	}
	return true;
}

// Load an ORCA file and return ORCA plain text and dimensions
bool AhabSim::convertFileToOrca(const std::string& path, std::string& outOrca, Usz& out_h, Usz& out_w) {
	Field tmp;
	field_init(&tmp);
	DEFER({ field_deinit(&tmp); });
	Field_load_error fle = field_load_file(path.c_str(), &tmp);
	if (fle != Field_load_error_ok) return false;
	std::string out;
	out.reserve((size_t)tmp.height * ((size_t)tmp.width + 1));
	for (Usz ry = 0; ry < tmp.height; ++ry) {
		const Glyph* row = tmp.buffer + ry * tmp.width;
		out.append((const char*)row, tmp.width);
		if (ry + 1 < tmp.height) out.push_back('\n');
	}
	outOrca = out;
	out_h = tmp.height; out_w = tmp.width;
	return true;
}

static std::unordered_map<void*, AhabSim*> callbackMap;

AhabSim::AhabSim()
	: tick_number_(0), random_seed_(0) {
	// Register this instance for callback from the sim. This is thread-safe as 
	// new instances are only created when the dsp engine is sychronized
	callbackMap[(void*)&oevent_list_] = this;

	// Initialize double buffers
	for (int i = 0; i < 2; ++i) {
		field_init(&field_[i]);
		field_init_fill(&field_[i], 1, 1, '.');
		mbuf_reusable_init(&mbuf_[i]);
		mbuf_reusable_ensure_size(&mbuf_[i], 1, 1);
	}
	oevent_list_init(&oevent_list_);
}

AhabSim::~AhabSim() {
	// Unregister this instance
	callbackMap.erase((void*)&oevent_list_);

	oevent_list_deinit(&oevent_list_);
	for (int i = 0; i < 2; ++i) {
		mbuf_reusable_deinit(&mbuf_[i]);
		field_deinit(&field_[i]);
	}

	// Clean up UDP device if created
	destroyUdpDev();
}

// UI thread operation - no locking needed (single UI thread assumed)
json_t* AhabSim::toJson() const {
	int widx = write_idx_.load(std::memory_order_relaxed);
	Field const* f = &field_[widx];
	json_t* j = json_object();
	json_object_set_new(j, "height", json_integer((int)f->height));
	json_object_set_new(j, "width", json_integer((int)f->width));
	json_object_set_new(j, "cells", json_string(rack::string::toBase64((const uint8_t*)f->buffer, f->height * f->width).c_str()));
	// Other properties
	json_object_set_new(j, "tick", json_integer((int)tick_number_.load()));
	json_object_set_new(j, "random_seed", json_integer((int)random_seed_));
	// UDP settings persisted with the sim
	json_object_set_new(j, "udpAddress", json_string(udpAddress_.c_str()));
	json_object_set_new(j, "udpPort", json_string(udpPort_.c_str()));
	// OSC settings persisted with the sim
	json_object_set_new(j, "oscAddress", json_string(oscAddress_.c_str()));
	json_object_set_new(j, "oscPort", json_string(oscPort_.c_str()));
	return j;
}

// UI thread operation - no locking needed (single UI thread assumed)
void AhabSim::fromJson(json_t* rootJ) {
	Field tmp;
	json_t* hJ = json_object_get(rootJ, "height");
	json_t* wJ = json_object_get(rootJ, "width");
	json_t* cellsJ = json_object_get(rootJ, "cells");
	if (!json_is_integer(hJ) || !json_is_integer(wJ) || !json_is_string(cellsJ)) return;
	int h = (int)json_integer_value(hJ);
	int w = (int)json_integer_value(wJ);
	if (h <= 0 || w <= 0) return;
	auto cells = rack::string::fromBase64(json_string_value(cellsJ));
	size_t cells_len = cells.size();
	field_init_fill(&tmp, (Usz)h, (Usz)w, '.');
	if (cells_len > 0) {
		size_t copy_len = std::min((size_t)h * (size_t)w, cells_len);
		memcpy(tmp.buffer, cells.data(), copy_len);
	}
	for (int i = 0; i < 2; ++i) {
		field_copy(&tmp, &field_[i]);
		mbuf_reusable_ensure_size(&mbuf_[i], tmp.height, tmp.width);
	}
	field_deinit(&tmp);

	// restore other properties
	int t = json_integer_value(json_object_get(rootJ, "tick"));
	tick_number_.store((Usz)t);
	int rs = json_integer_value(json_object_get(rootJ, "random_seed"));
	random_seed_ = (Usz)rs;
	// Restore UDP settings if present
	json_t* addrJ = json_object_get(rootJ, "udpAddress");
	json_t* portJ = json_object_get(rootJ, "udpPort");
	std::string addr;
	std::string port;
	if (json_is_string(addrJ)) addr = json_string_value(addrJ);
	if (json_is_string(portJ)) port = json_string_value(portJ);
	if (!addr.empty() || !port.empty()) {
		setUdpDestination(addr, port);
	}
	// Restore OSC settings if present
	json_t* oscAddrJ = json_object_get(rootJ, "oscAddress");
	json_t* oscPortJ = json_object_get(rootJ, "oscPort");
	std::string oscAddr;
	std::string oscPort;
	if (json_is_string(oscAddrJ)) oscAddr = json_string_value(oscAddrJ);
	if (json_is_string(oscPortJ)) oscPort = json_string_value(oscPortJ);
	if (!oscAddr.empty() || !oscPort.empty()) {
		setOscDestination(oscAddr, oscPort);
	}
}

// UI thread operation - load file into a temporary buffer, then schedule a ReplaceField command
bool AhabSim::loadFromFileRequest(const std::string& path) {
	if (ui_cmd_queue_.full()) return false;
	Field tmp;
	field_init(&tmp);
	DEFER({ field_deinit(&tmp); });
	Field_load_error fle = field_load_file(path.c_str(), &tmp);
	if (fle != Field_load_error_ok) return false;

	UiCommand* cmd = new UiCommand();
	cmd->type = UiCommandType::REPLACE_FIELD;
	cmd->new_h = tmp.height; cmd->new_w = tmp.width;
	size_t sz = (size_t)tmp.height * (size_t)tmp.width;
	cmd->cells = (Glyph*)malloc(sz);
	memcpy(cmd->cells, tmp.buffer, sz);
	pushUndo();
	ui_cmd_queue_.push(cmd);
	notifyTick();
	return true;
}

// UI thread operation - no locking needed
bool AhabSim::saveToFile(const std::string& path) {
	FILE* f = fopen(path.c_str(), "w");
	if (!f) {
		return false;
	}
	field_fput(&field_[0], f);
	if (fclose(f) != 0) {
		return false;
	}
	return true;
} 

// UI thread operation - paste ORCA plain text into the field (enqueue a UI command)
bool AhabSim::loadRectFromOrcaRequest(const std::string& orcaStr, Usz dest_y, Usz dest_x, Usz& out_h, Usz& out_w, bool replace_field) {
	if (ui_cmd_queue_.full()) return false;
	out_h = out_w = 0;
	Field tmp;
	field_init(&tmp);
	DEFER({ field_deinit(&tmp); });
	if (!buildFieldFromOrcaText(orcaStr, tmp)) return false;
	int h = (int)tmp.height;
	int w = (int)tmp.width;

	// For paste (non-replace) validate against displayed field and compute clipped size early
	Usz copy_h = (Usz)h;
	Usz copy_w = (Usz)w;
	if (!replace_field) {
		int ridx = read_idx_.load(std::memory_order_acquire);
		Usz fh = field_[ridx].height;
		Usz fw = field_[ridx].width;
		if (dest_y >= fh || dest_x >= fw) return false;
		if (dest_y + copy_h > fh) copy_h = fh - dest_y;
		if (dest_x + copy_w > fw) copy_w = fw - dest_x;
		if (copy_h == 0 || copy_w == 0) return false;
	}

	size_t sz = (size_t)tmp.height * (size_t)tmp.width;
	UiCommand* cmd = new UiCommand();
	cmd->cells = (Glyph*)malloc(sz);
	if (!cmd->cells) { delete cmd; return false; }
	memcpy(cmd->cells, tmp.buffer, sz);

	if (replace_field) {
		cmd->type = UiCommandType::REPLACE_FIELD;
		cmd->new_h = tmp.height; cmd->new_w = tmp.width;
		out_h = (Usz)cmd->new_h;
		out_w = (Usz)cmd->new_w;
	} else {
		cmd->type = UiCommandType::PASTE_CELLS;
		cmd->y = dest_y; cmd->x = dest_x; cmd->new_h = (Usz)h; cmd->new_w = (Usz)w;
		out_h = copy_h;
		out_w = copy_w;
	}

	pushUndo();
	ui_cmd_queue_.push(cmd);
	notifyTick();
	return true;
}

// Serialize a rectangular region into ORCA plain text
std::string AhabSim::convertRectToOrca(Usz y, Usz x, Usz h, Usz w) const {
	int ridx = read_idx_.load(std::memory_order_acquire);
	if (ridx < 0 || ridx > 1) return std::string();
	Usz fh = field_[ridx].height;
	Usz fw = field_[ridx].width;
	if (fh == 0 || fw == 0) return std::string();
	if (y >= fh || x >= fw) return std::string();
	if (y + h > fh) h = fh - y;
	if (x + w > fw) w = fw - x;
	std::string out;
	out.reserve((size_t)h * ((size_t)w + 1));
	for (Usz ry = 0; ry < h; ++ry) {
		const Glyph* row = field_[ridx].buffer + (y + ry) * fw;
		for (Usz cx = 0; cx < w; ++cx) out.push_back(row[x + cx]);
		if (ry + 1 < h) out.push_back('\n');
	}
	return out;
}

// UI thread operation - no locking needed (single UI thread assumed)
bool AhabSim::pushUndo() {
	if (undo_limit_ == 0) return false;
	int widx = write_idx_.load(std::memory_order_relaxed);
	// If limit reached, pop front of undo_history_
	if (undo_history_.size() == undo_limit_) {
		field_deinit(&undo_history_.front().f);
		undo_history_.pop_front();
	}
	// New user edits clear redo history
	while (!redo_history_.empty()) {
		field_deinit(&redo_history_.front().f);
		redo_history_.pop_front();
	}
	UndoNode node;
	field_init(&node.f);
	field_copy(&field_[widx], &node.f);
	node.tick = tick_number_.load();
	undo_history_.push_back(std::move(node));
	return true;
}

int AhabSim::getDisplayBuffer(Usz& height, Usz& width) const {
	// Lock-free read from widget thread
	int ridx = read_idx_.load(std::memory_order_acquire);
	height = field_[ridx].height;
	width = field_[ridx].width;
	return ridx;
}

Glyph const* AhabSim::getFieldBuffer(int idx) const {
	return field_[idx].buffer;
}

Mark const* AhabSim::getMbufBuffer(int idx) const {
	return mbuf_[idx].buffer;
}

void AhabSim::notifyTick() {
	// UI thread requests a display publish. Do not perform the swap here (to
	// avoid races with DSP). DSP thread must call `process()` before `step()`
	// to publish the current write buffer for redraw.
	pending_ui_update_.store(true, std::memory_order_release);
}

// UI thread operation - enqueue a CutRect command
void AhabSim::cutRectRequest(Usz y, Usz x, Usz h, Usz w) {
	if (ui_cmd_queue_.full()) return;
	UiCommand* cmd = new UiCommand();
	cmd->type = UiCommandType::CUT_RECT; cmd->y = y; cmd->x = x; cmd->h = h; cmd->w = w;
	pushUndo();
	ui_cmd_queue_.push(cmd);
	notifyTick();
}

// DSP thread operation - cut (clear) a rectangular region in both field buffers
bool AhabSim::cutRect(Usz y, Usz x, Usz h, Usz w) {
	// Simply clear the specified region in both buffers (clipboard was set on UI thread before issuing the request).
	for (int i = 0; i < 2; ++i) {
		if (y < field_[i].height && x < field_[i].width) {
			Usz clip_h = h;
			Usz clip_w = w;
			if (y + clip_h > field_[i].height) clip_h = field_[i].height - y;
			if (x + clip_w > field_[i].width) clip_w = field_[i].width - x;
			if (clip_h > 0 && clip_w > 0) {
				gbuffer_fill_subrect(field_[i].buffer, field_[i].height, field_[i].width, y, x, clip_h, clip_w, '.');
			}
		}
	}
	return true;
}

// UI thread operation - enqueue a FillRect command
void AhabSim::fillRectRequest(Usz y, Usz x, Usz h, Usz w, Glyph fill) {
	if (ui_cmd_queue_.full()) return;
	UiCommand* cmd = new UiCommand(); 
	cmd->type = UiCommandType::FILL_RECT; 
	cmd->y = y; cmd->x = x; cmd->h = h; cmd->w = w; cmd->g = fill;
	pushUndo();
	ui_cmd_queue_.push(cmd);
	notifyTick();
}

// DSP thread operation - fill a rectangular region in both field buffers
bool AhabSim::fillRect(Usz y, Usz x, Usz h, Usz w, Glyph fill) {
	// Check dimensions from first buffer
	if (y >= field_[0].height || x >= field_[0].width) {
		return false;
	}
	if (y + h > field_[0].height) h = field_[0].height - y;
	if (x + w > field_[0].width) w = field_[0].width - x;
	// Fill both buffers
	for (int i = 0; i < 2; ++i) {
		gbuffer_fill_subrect(field_[i].buffer, field_[i].height, field_[i].width, y, x, h, w, fill);
	}
	return true;
}

// UI thread operation - enqueue a MoveRect command
void AhabSim::moveRectRequest(Usz y, Usz x, Usz h, Usz w, Isz dest_y, Isz dest_x) {
	if (ui_cmd_queue_.full()) return;
	UiCommand* cmd = new UiCommand(); 
	cmd->type = UiCommandType::MOVE_RECT; 
	cmd->y = y; cmd->x = x; cmd->h = h; cmd->w = w; 
	cmd->new_h = (Usz)dest_y;
	cmd->new_w = (Usz)dest_x;
	pushUndo();
	ui_cmd_queue_.push(cmd);
	notifyTick();
}

// DSP thread operation - move a rectangular region within both field buffers
bool AhabSim::moveRect(Usz y, Usz x, Usz h, Usz w, Isz dest_y, Isz dest_x) {
	int widx = write_idx_.load(std::memory_order_relaxed);
	// Validate source
	if (y >= field_[widx].height || x >= field_[widx].width) {
		return false;
	}
	// clip source
	if (y + h > field_[widx].height) h = field_[widx].height - y;
	if (x + w > field_[widx].width) w = field_[widx].width - x;
	if (h == 0 || w == 0) {
		return false;
	}

	// Allocate temporary buffer on the stack for the move operation
	size_t sz = (size_t)h * (size_t)w;
	Glyph* tmpbuf = (Glyph*)alloca(sz * sizeof(Glyph));

	// Copy source region into a temporary buffer
	gbuffer_copy_subrect(field_[widx].buffer, tmpbuf, field_[widx].height, field_[widx].width,
			h, w, y, x, 0, 0, h, w);

	// Clear original region in both buffers
	for (int i = 0; i < 2; ++i) {
		gbuffer_fill_subrect(field_[i].buffer, field_[i].height, field_[i].width, y, x, h, w, '.');
	}

	// Compute source offsets in tmp buffer and destination clipping
	Isz dy = dest_y;
	Isz dx = dest_x;
	// Destination top-left in signed coords
	Isz dst_y = dy;
	Isz dst_x = dx;
	// If destination is entirely outside the field, nothing to paste
	// Compute initial source offsets into tmp buffer (src_off_y/src_off_x)
	Usz src_off_y = 0, src_off_x = 0;
	Usz copy_h = h, copy_w = w;

	// Adjust for negative destination (skip rows/cols from top/left of tmp buffer)
	if (dst_y < 0) {
		Usz skip = (Usz)(-dst_y);
		if (skip >= copy_h) {
			return true; // nothing fits
		}
		src_off_y = skip;
		copy_h -= skip;
		dst_y = 0;
	}
	if (dst_x < 0) {
		Usz skip = (Usz)(-dst_x);
		if (skip >= copy_w) {
			return true;
		}
		src_off_x = skip;
		copy_w -= skip;
		dst_x = 0;
	}
	// Clip bottom/right
	if ((Usz)dst_y + copy_h > field_[widx].height) copy_h = field_[widx].height - (Usz)dst_y;
	if ((Usz)dst_x + copy_w > field_[widx].width) copy_w = field_[widx].width - (Usz)dst_x;
	if (copy_h == 0 || copy_w == 0) {
		return true;
	}

	// Copy from tmp buffer into both field buffers at dst position
	for (int i = 0; i < 2; ++i) {
		gbuffer_copy_subrect(tmpbuf, field_[i].buffer, h, w,
				field_[i].height, field_[i].width, src_off_y, src_off_x, (Usz)dst_y, (Usz)dst_x, copy_h, copy_w);
	}
	return true;
}

// UI thread operation - enqueue a SetFieldSize command
void AhabSim::setFieldSizeRequest(Usz h, Usz w, bool doUndo) {
	if (ui_cmd_queue_.full()) return;
	UiCommand* cmd = new UiCommand();
	cmd->type = UiCommandType::SET_FIELD_SIZE;
	cmd->new_h = h; cmd->new_w = w;
	if (doUndo) pushUndo();
	ui_cmd_queue_.push(cmd);
	notifyTick();
}

// DSP thread operation - resize both field buffers, preserving overlapping region
void AhabSim::setFieldSize(Usz height, Usz width) {
	// Enforce maximum field size to prevent stack overflow from alloca.
	// UI limits are 97x49, but clamp here for safety (max ~10KB on stack).
	const Usz MAX_HEIGHT = 100;
	const Usz MAX_WIDTH = 100;
	if (height > MAX_HEIGHT) height = MAX_HEIGHT;
	if (width > MAX_WIDTH) width = MAX_WIDTH;
	if (height == 0) height = 1;
	if (width == 0) width = 1;

	int widx = write_idx_.load(std::memory_order_relaxed);
	// Create a new field filled with '.' and copy the overlapping region from the current field
	// Use a stack-allocated temporary buffer to avoid heap allocations on the DSP thread.
	size_t sz = (size_t)height * (size_t)width;
	Glyph* tmpbuf = (Glyph*)alloca(sz * sizeof(Glyph));
	memset(tmpbuf, '.', sz * sizeof(Glyph));

	Usz old_h = field_[widx].height;
	Usz old_w = field_[widx].width;
	Usz min_h = height < old_h ? height : old_h;
	Usz min_w = width < old_w ? width : old_w;
	for (Usz ry = 0; ry < min_h; ++ry) {
		Glyph *dst_row = tmpbuf + ry * width;
		Glyph *src_row = field_[widx].buffer + ry * old_w;
		memcpy(dst_row, src_row, min_w * sizeof(Glyph));
	}

	// Create a lightweight Field view over the stack buffer so we can reuse field_copy
	Field tmp;
	tmp.buffer = tmpbuf;
	tmp.height = (U16)height;
	tmp.width = (U16)width;
	// Copy tmp into both field buffers
	for (int i = 0; i < 2; ++i) {
		field_copy(&tmp, &field_[i]);
		mbuf_reusable_ensure_size(&mbuf_[i], height, width);
		if (mbuf_[i].buffer) {
			mbuf_uninit_mark(mbuf_[i].buffer, height, width);
		}
	}
}

// UI thread operation
void AhabSim::undoRequest() {
	if (ui_cmd_queue_.full()) return;
	if (undo_history_.empty()) return; // Nothing to undo

	UiCommand* cmd = new UiCommand(); 
	if (!cmd) return;
	cmd->type = UiCommandType::UNDO;  

	// Pre-allocate a buffer for the redo operation on the UI thread
	int ridx = read_idx_.load(std::memory_order_relaxed);
	size_t sz = (size_t)field_[ridx].height * (size_t)field_[ridx].width;
	Glyph* tmpbuf = (Glyph*)malloc(sz);
	if (!tmpbuf) { delete cmd; return; }
	cmd->cells = tmpbuf;

	ui_cmd_queue_.push(cmd);
	notifyTick();
}

// DSP thread operation, transfers ownership of redoBuf from UI thread
void AhabSim::undo(Glyph* redoBuf) {
	UndoNode node;
	if (undo_history_.empty()) return;
	// Save current state into redo history
	{
		int widx = write_idx_.load(std::memory_order_relaxed);
		// Use the provided temporary buffer for the current field copy
		UndoNode cur;
		cur.f.buffer = redoBuf;
		cur.f.height = field_[widx].height;
		cur.f.width = field_[widx].width;
		field_copy(&field_[widx], &cur.f);
		cur.tick = tick_number_.load();
		// enforce redo size limit (match undo_limit_)
		if (redo_history_.size() == undo_limit_) {
			field_deinit(&redo_history_.front().f);
			redo_history_.pop_front();
		}
		redo_history_.push_back(std::move(cur));
	}
	// Pop last undo and apply it
	node = std::move(undo_history_.back());
	undo_history_.pop_back();
	// Apply into both buffers for consistency
	for (int i = 0; i < 2; ++i) {
		field_copy(&node.f, &field_[i]);
		mbuf_reusable_ensure_size(&mbuf_[i], field_[i].height, field_[i].width);
	}
	tick_number_.store(node.tick);
	// cleanup node's field memory
	field_deinit(&node.f);
}

// UI thread operation
void AhabSim::redoRequest() {
	if (ui_cmd_queue_.full()) return;
	if (redo_history_.empty()) return; // Nothing to redo

	UiCommand* cmd = new UiCommand();
	if (!cmd) return;
	cmd->type = UiCommandType::REDO;

	// Pre-allocate a buffer for the undo operation on the UI thread
	int ridx = read_idx_.load(std::memory_order_relaxed);
	size_t sz = (size_t)field_[ridx].height * (size_t)field_[ridx].width;
	Glyph* tmpbuf = (Glyph*)malloc(sz);
	if (!tmpbuf) { delete cmd; return; }
	cmd->cells = tmpbuf;

	ui_cmd_queue_.push(cmd);

	notifyTick();
}

// DSP thread operation, transfers ownership of undoBuf from UI thread
void AhabSim::redo(Glyph* undoBuf) {
	UndoNode node;
	if (redo_history_.empty()) return;
	// Save current state into undo history
	{
		int widx = write_idx_.load(std::memory_order_relaxed);
		// Use the provided temporary buffer for the current field copy
		UndoNode cur;
		cur.f.buffer = undoBuf;
		cur.f.height = field_[widx].height;
		cur.f.width = field_[widx].width;
		field_copy(&field_[widx], &cur.f);
		cur.tick = tick_number_.load();
		if (undo_history_.size() == undo_limit_) {
			field_deinit(&undo_history_.front().f);
			undo_history_.pop_front();
		}
		undo_history_.push_back(std::move(cur));
	}
	// Pop last redo and apply it
	node = std::move(redo_history_.back());
	redo_history_.pop_back();
	// Apply into both buffers for consistency
	for (int i = 0; i < 2; ++i) {
		field_copy(&node.f, &field_[i]);
		mbuf_reusable_ensure_size(&mbuf_[i], field_[i].height, field_[i].width);
	}
	tick_number_.store(node.tick);
	// cleanup node's field memory
	field_deinit(&node.f);
}

// UI thread operation
bool AhabSim::canUndo() const {
	return !undo_history_.empty();
}

// UI thread operation
Usz AhabSim::getUndoCount() const {
	return (Usz)undo_history_.size();
}

// UI thread operation
void AhabSim::setUndoLimit(Usz limit) {
	undo_limit_ = limit;
	while (undo_history_.size() > undo_limit_) {
		field_deinit(&undo_history_.front().f);
		undo_history_.pop_front();
	}
	while (redo_history_.size() > undo_limit_) {
		field_deinit(&redo_history_.front().f);
		redo_history_.pop_front();
	}
}

// UI thread operation
void AhabSim::resetUndo() {
	// Clear undo and redo history
	while (!undo_history_.empty()) {
		field_deinit(&undo_history_.front().f);
		undo_history_.pop_front();
	}
	while (!redo_history_.empty()) {
		field_deinit(&redo_history_.front().f);
		redo_history_.pop_front();
	}
}

// UI thread operation - enqueue a Reset command
void AhabSim::resetRequest() {
	if (ui_cmd_queue_.full()) return;
	UiCommand* cmd = new UiCommand(); 
	cmd->type = UiCommandType::RESET;  
	pushUndo();
	ui_cmd_queue_.push(cmd);
	notifyTick();
}

// DSP thread operation
void AhabSim::reset() {
	// clear state while keeping current dimensions
	int widx = write_idx_.load(std::memory_order_relaxed);
	Usz h = field_[widx].height;
	Usz w = field_[widx].width;

	// Clear both field buffers and mark buffers
	for (int i = 0; i < 2; ++i) {
		if (h > 0 && w > 0) {
			gbuffer_fill_subrect(field_[i].buffer, field_[i].height, field_[i].width, 0, 0, h, w, '.');
		}
		if (mbuf_[i].buffer) {
			mbuffer_clear(mbuf_[i].buffer, field_[i].height, field_[i].width);
			mbuf_uninit_mark(mbuf_[i].buffer, field_[i].height, field_[i].width);
		}
	}

	// Clear event list and reset tick counter
	oevent_list_clear(&oevent_list_);
	tick_number_.store(0);

	// Reset indices to canonical values
	write_idx_.store(0, std::memory_order_relaxed);
	read_idx_.store(1, std::memory_order_release);

	auto cb1 = std::atomic_load(&ui_reset_callback_ptr_);
	if (cb1 && *cb1) (*cb1)();
}

// UI thread operation - enqueue an setGlyph command
void AhabSim::setGlyphRequest(Usz y, Usz x, Glyph g, Mark_flags flags, bool doUndo) {
	if (ui_cmd_queue_.full()) return;
	UiCommand* cmd = new UiCommand();
	cmd->type = UiCommandType::APPLY_GLYPH; 
	cmd->y = y; cmd->x = x; cmd->g = g; cmd->flags = flags;
	if (doUndo) pushUndo();
	ui_cmd_queue_.push(cmd);
	notifyTick();
}

// DSP thread operation - set a glyph at (y,x) in both field buffers, applying mark flags
void AhabSim::setGlyph(Usz y, Usz x, Glyph g, Mark_flags flags) {
	field_[0].buffer[y * field_[0].width + x] = g;
	mbuf_[0].buffer[y * field_[0].width + x] |= (Mark)flags;
	field_[1].buffer[y * field_[1].width + x] = g;
	mbuf_[1].buffer[y * field_[1].width + x] |= (Mark)flags;
}

// DSP thread operation
// Paste a raw cells buffer of size h x w into the field at (dest_y,dest_x).
// Clips to the destination field; returns true if any cells were written.
bool AhabSim::pasteCells(Glyph* cells, Usz h, Usz w, Usz dest_y, Usz dest_x) {
	if (!cells || h == 0 || w == 0) return false;
	int widx = write_idx_.load(std::memory_order_relaxed);
	Usz max_h = field_[widx].height;
	Usz max_w = field_[widx].width;
	if (dest_y >= max_h || dest_x >= max_w) return false;
	// Clip paste region
	Usz copy_h = h;
	Usz copy_w = w;
	Usz src_off_y = 0, src_off_x = 0;
	if (dest_y + copy_h > max_h) copy_h = max_h - dest_y;
	if (dest_x + copy_w > max_w) copy_w = max_w - dest_x;
	if (copy_h == 0 || copy_w == 0) return false;
	for (int i = 0; i < 2; ++i) {
		gbuffer_copy_subrect(cells, field_[i].buffer, h, w,
			field_[i].height, field_[i].width, src_off_y, src_off_x, dest_y, dest_x, copy_h, copy_w);
	}
	return true;
}

// DSP thread operation
void AhabSim::replaceField(Glyph* cells, Usz nh, Usz nw) {
	for (int i = 0; i < 2; ++i) {
		mbuf_reusable_ensure_size(&mbuf_[i], nh, nw);
		mbuf_uninit_mark(mbuf_[i].buffer, nh, nw);
		Field tmp;
		tmp.buffer = cells;
		tmp.height = (U16)nh;
		tmp.width = (U16)nw;
		field_copy(&tmp, &field_[i]);
	}
	tick_number_.store(0);
}


Usz AhabSim::getFieldHeight() const {
	// Lock-free read from any thread
	int ridx = read_idx_.load(std::memory_order_acquire);
	return (Usz)field_[ridx].height;
}

Usz AhabSim::getFieldWidth() const {
	// Lock-free read from any thread
	int ridx = read_idx_.load(std::memory_order_acquire);
	return (Usz)field_[ridx].width;
}

// UI thread operation
void AhabSim::stepRequest() {
	if (ui_cmd_queue_.full()) return;
	UiCommand* cmd = new UiCommand(); 
	cmd->type = UiCommandType::STEP;  
	ui_cmd_queue_.push(cmd);
	notifyTick();
}

// New lock-free step() implementation - called from DSP thread
void AhabSim::step() {
	// Get current write buffer index
	int widx = write_idx_.load(std::memory_order_relaxed);
	
	// Ensure mbuf is large enough for this field (no lock needed, write buffer exclusive to DSP thread)
	mbuf_reusable_ensure_size(&mbuf_[widx], field_[widx].height, field_[widx].width);
	mbuffer_clear(mbuf_[widx].buffer, field_[widx].height, field_[widx].width);
	oevent_list_clear(&oevent_list_);

	Usz t = tick_number_.load(std::memory_order_relaxed);
	orca_run(field_[widx].buffer, mbuf_[widx].buffer, field_[widx].height, field_[widx].width, t, &oevent_list_, random_seed_);
	tick_number_.fetch_add(1, std::memory_order_relaxed);

	// Copy write buffer into the current read buffer, then swap indices so
	// the freshly written state becomes available to the UI.
	int ridx = read_idx_.load(std::memory_order_relaxed);
	// Ensure destination mbuf fits
	field_copy(&field_[widx], &field_[ridx]);
	mbuf_reusable_ensure_size(&mbuf_[ridx], field_[ridx].height, field_[ridx].width);
	if (mbuf_[widx].buffer && mbuf_[ridx].buffer) {
		size_t elems = (size_t)field_[ridx].height * (size_t)field_[ridx].width;
		memcpy(mbuf_[ridx].buffer, mbuf_[widx].buffer, elems * sizeof(Mark));
	}

	// Publish by swapping indices (read index becomes the just-written one)
	read_idx_.store(widx, std::memory_order_release);
	write_idx_.store(ridx, std::memory_order_relaxed);

	// Handle OSC and UDP events directly in the sim so they don't need to be
	// processed by the module layer.
	for (Usz ei = 0; ei < oevent_list_.count; ++ei) {
		Oevent const *oe = &oevent_list_.buffer[ei];
		switch ((Oevent_types)oe->any.oevent_type) {
			case Oevent_type_osc_ints: {
				Oevent_osc_ints const *eo = &oe->osc_ints;
				// Build OSC address from configured prefix + glyph
				std::string addr = {'/', eo->glyph, '\0'};
				if (eo->count > 0) {
					I32 vals[Oevent_osc_int_count];
					for (Usz j = 0; j < (Usz)eo->count; ++j) vals[j] = (I32)eo->numbers[j];
					sendOscInts(addr.c_str(), vals, (Usz)eo->count);
				} else {
					sendOscInts(addr.c_str(), nullptr, 0);
				}
				break;
			}
			case Oevent_type_udp_string: {
				Oevent_udp_string const *ud = &oe->udp_string;
				if (ud && ud->count > 0) {
					sendUdpDatagram(ud->chars, (Usz)ud->count);
				}
				break;
			}
			default:
				// handled by module layer
				break;
		}
	}

	// Call the callback without holding any locks - widget can read from read_idx
	auto cb1 = std::atomic_load(&ui_tick_callback_ptr_);
	if (cb1 && *cb1) {
	int callback_idx = read_idx_.load(std::memory_order_acquire);
		(*cb1)(&field_[callback_idx]);
	}
	auto cb2 = std::atomic_load(&dsp_tick_callback_ptr_);
	if (cb2 && *cb2) {
		(*cb2)(&oevent_list_);
	}
}

// Process pending UI requests. Must be called from DSP thread.
void AhabSim::process() {
	// Fast-path: nothing pending
	if (!pending_ui_update_.load(std::memory_order_acquire)) return;

	// Clear the request
	pending_ui_update_.store(false, std::memory_order_relaxed);

	// Drain UI->DSP command queue and apply each command on the DSP/write buffer.
	UiCommand* cmd = nullptr;
	while (!ui_cmd_queue_.empty()) {
		cmd = ui_cmd_queue_.shift();
		if (!cmd) continue;
		switch (cmd->type) {
			case UiCommandType::APPLY_GLYPH: {
				setGlyph(cmd->y, cmd->x, cmd->g, cmd->flags);
				break;
			}
			case UiCommandType::SET_FIELD_SIZE: {
				setFieldSize(cmd->new_h, cmd->new_w);
				break;
			}
			case UiCommandType::REPLACE_FIELD: {
				replaceField(cmd->cells, cmd->new_h, cmd->new_w);
				break;
			}
			case UiCommandType::CUT_RECT: {
				cutRect(cmd->y, cmd->x, cmd->h, cmd->w);
				break;
			}
			case UiCommandType::PASTE_CELLS: {
				pasteCells(cmd->cells, cmd->new_h, cmd->new_w, cmd->y, cmd->x);
				break;
			}
			case UiCommandType::FILL_RECT: {
				fillRect(cmd->y, cmd->x, cmd->h, cmd->w, cmd->g);
				break;
			}
			case UiCommandType::MOVE_RECT: {
				moveRect(cmd->y, cmd->x, cmd->h, cmd->w, (Isz)cmd->new_h, (Isz)cmd->new_w);
				break;
			}
			case UiCommandType::STEP: {
				step();
				break;
			}
			case UiCommandType::RESET: {
				reset();
				break;
			}
			case UiCommandType::UNDO: {
				undo(cmd->cells);
				cmd->cells = nullptr; // ownership transferred
				break;
			}
			case UiCommandType::REDO: {
				redo(cmd->cells);
				cmd->cells = nullptr; // ownership transferred
				break;
			}
			default: break;
		}
		delete cmd;
	}

	auto cb1 = std::atomic_load(&ui_tick_callback_ptr_);
	if (cb1 && *cb1) {
		int callback_idx = read_idx_.load(std::memory_order_acquire);
		(*cb1)(&field_[callback_idx]);
	}
}

// UDP helper: ensure device, send, destroy
bool AhabSim::ensureUdpDev(const char* addr, const char* port) {
	if (udp_dev_) return true;
	if (oosc_dev_create_udp(&udp_dev_, addr, port) != Oosc_udp_create_error_ok) {
		udp_dev_ = nullptr;
		return false;
	}
	// store the destination
	udpAddress_ = addr ? addr : std::string();
	udpPort_ = port ? port : std::string();
	return true;
}

void AhabSim::destroyUdpDev() {
	if (udp_dev_) {
		oosc_dev_destroy(udp_dev_);
		udp_dev_ = nullptr;
	}
}


void AhabSim::setUdpDestination(const std::string& address, const std::string& port) {
	std::string newAddr = address;
	std::string newPort = port;
	trimStr(newAddr);
	trimStr(newPort);
	if (!isValidPort(newPort)) return;
	if (!newAddr.empty() && containsWhitespace(newAddr)) return;

	udpAddress_ = newAddr;
	udpPort_ = newPort;
	destroyUdpDev();
}

void AhabSim::sendUdpDatagram(const char* data, Usz size) {
	// If the sim has a configured destination, use it; otherwise try defaults
	if (!udp_dev_) {
		if (!udpAddress_.empty()) {
			if (!ensureUdpDev(udpAddress_.c_str(), udpPort_.c_str())) return;
		} else {
			if (!ensureUdpDev("127.0.0.1", "49161")) return;
		}
	}
	oosc_send_datagram(udp_dev_, data, size);
}

void AhabSim::setOscDestination(const std::string& address, const std::string& port) {
	std::string newAddr = address;
	std::string newPort = port;
	trimStr(newAddr);
	trimStr(newPort);
	if (!isValidPort(newPort)) return;
	if (!newAddr.empty() && containsWhitespace(newAddr)) return;

	oscAddress_ = newAddr;
	oscPort_ = newPort;
	destroyUdpDev();
}

void AhabSim::sendOscInts(const char* osc_path, I32 const* vals, Usz count) {
	// Ensure UDP device exists (respect configured destination or use defaults)
	if (!udp_dev_) {
		if (!oscAddress_.empty()) {
			if (!ensureUdpDev(oscAddress_.c_str(), oscPort_.c_str())) return;
		} else {
			if (!ensureUdpDev("127.0.0.1", "49161")) return;
		}
	}
	oosc_send_int32s(udp_dev_, osc_path, vals, count);
}


// Callback function for operator '<' to read CV port value
extern "C" Usz custom_vcvin(void* ptr, Usz port_num, Usz a, Usz b) {
	auto it = callbackMap.find(ptr);
	if (it == callbackMap.end()) return 0;
	AhabSim* sim = it->second;

	// Numeric ports '1'..'4' -> index 1..4
	if (port_num >= 1 && port_num <= 4) {
		float v = sim->readDspInput(port_num - 1);
		float s = std::min(std::max(0.0f, std::roundf(v * 3.5f)), 35.0f);
		Usz val = Usz(s * float(b - a) / 35.f) + a;
		return val;
	}

	// Letter ports 'a'..'d' -> map V/oct to nearest note glyph
	if (port_num >= 10 && port_num <= 13) {
		float v = sim->readDspInput(port_num - 10);
		int semitone = int(std::roundf(v * 12.0f));
		int s12 = semitone % 12;
		return s12;
	}

	return 0;
}

float AhabSim::readDspInput(size_t port_num) const {
	auto cb = std::atomic_load(&dsp_input_reader_ptr_);
	if (cb && *cb) return (*cb)(port_num);
	return 0;
}

// Callback function for operator '>' to write CV port value
extern "C" void custom_vcvout(void* ptr, Usz port_index, Usz a, Usz b, Usz value) {
	auto it = callbackMap.find(ptr);
	if (it == callbackMap.end()) return;
	AhabSim* sim = it->second;

	// Numeric ports '1'..'4' => port_num 0..3.
	// value is glyph index (0..35). Voltage should be step / 3.5 (so 35 -> 10V).
	// a = min, b = max
	if (port_index >= 1 && port_index <= 4) {
		Usz s = std::min(std::max(a, value), b);
		float voltage = 0.0f;
		if (b > a) {
			voltage = float(s - a) / float(b - a) * 10.0f;
		}
		sim->writeDspOutput(port_index - 1, voltage);
		return;
	}

	// Letter ports 'a'..'d' => interpret value as semitone and convert to V/oct (1V per octave = 12 semitones)
	// a = octave, b = ignored
	if (port_index >= 10 && port_index <= 13) {
		float voltage = float(value + a * 12) / 12.0f;
		sim->writeDspOutput(port_index - 10, voltage);
		return;
	}
}

void AhabSim::writeDspOutput(size_t port_num, float value) {
	auto cb = std::atomic_load(&dsp_output_writer_ptr_);
	if (cb && *cb) (*cb)(port_num, value);
}

} // namespace Ahab
} // namespace StoermelderPackOne