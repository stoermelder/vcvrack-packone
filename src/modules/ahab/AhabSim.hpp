#pragma once
#include <functional>
#include <atomic>
#include <string>
#include <jansson.h>
#include <memory>
#include <deque>
#include <vector>
#include <cstdint>
#include <dsp/ringbuffer.hpp>

extern "C" {
	#include "../../../orca-c/field.h"
	#include "../../../orca-c/gbuffer.h"
	#include "../../../orca-c/vmio.h"
	#include "../../../orca-c/sim.h"
	#include "../../../orca-c/osc_out.h"
}

namespace StoermelderPackOne {
namespace Ahab {

// A C++ wrapper around the Orca VM that can drive the simulation
// (single-step). It owns a Field buffer (grid), an mbuffer for per-cell marks, and an Oevent_list for
// emitted events.

class AhabSim {
public:
	// Load an ORCA file and serialize it to ORCA plain text (identical to file contents
	// except trailing newline handling). Returns true and fills outOrca/out_h/out_w on success.
	static bool convertFileToOrca(const std::string& path, std::string& outOrca, Usz& out_h, Usz& out_w);

	// Parse ORCA plain text into a Field object. Returns true on success.
	static bool buildFieldFromOrcaText(const std::string& orcaText, Field& out);

	AhabSim();
	~AhabSim();

	// Non-copyable
	AhabSim(const AhabSim&) = delete;
	AhabSim& operator=(const AhabSim&) = delete;

	// Callback type for tick notifications to the UI object
	using UiTickCallback = std::function<void(Field const*)>;
	// Reset callback: used to notify when the sim has been reset
	using UiResetCallback = std::function<void()>;
	// Callback type for tick notifications to the DSP object
	using TickDspCallback = std::function<void(Oevent_list const*)>;
	// Input reader callback: used to query the module inputs (e.g. for vcvin)
	using DspInputReader = std::function<float(size_t port_num)>;
	// Output writer callback: used to set module outputs (e.g. for vcvout)
	using DspOutputWriter = std::function<void(size_t port_num, float value)>;

	// Load a grid from file (wrapper around field_load_file). Returns true on
	// success.
	bool loadFromFileRequest(const std::string& path);

	// Paste ORCA text (plain .orca format) at the given destination coordinates.
	bool loadRectFromOrcaRequest(const std::string& orcaStr, Usz dest_y, Usz dest_x, Usz& out_h, Usz& out_w, bool replace_field = false);

	// Save the current grid to a file. Returns true on success.
	bool saveToFile(const std::string& path);

	// Replace the current field with provided cells of given dimensions.
	void replaceField(Glyph* cells, Usz new_h, Usz new_w);

	// Serialize a rectangular region of the displayed field to ORCA plain text.
	// Each row will be written as a line terminated by '\n'.
	std::string convertRectToOrca(Usz y, Usz x, Usz h, Usz w) const;

	// Single-step the VM (one tick). Increments internal tick counter and
	// invokes the tick callback if set. Called from DSP thread - must be lock-free.
	void step();
	void stepRequest();

	// Process any pending UI requests (to be called from DSP thread before stepping).
	// If notifyTick() was called from the UI, `process()` will publish the current
	// write buffer for display (without advancing the simulation) and invoke the
	// tick callback with step_happened == false.
	void process();

	// Set RNG seed used by `orca_run`.
	void setRandomSeed(Usz seed) { random_seed_ = seed; }
	Usz getRandomSeed() const { return random_seed_; }

	void setUiTickCallback(UiTickCallback cb) {
		if (cb)
			std::atomic_store(&ui_tick_callback_ptr_, std::make_shared<UiTickCallback>(std::move(cb)));
		else
			std::atomic_store(&ui_tick_callback_ptr_, std::shared_ptr<UiTickCallback>());
	}

	void setDspTickCallback(TickDspCallback cb) {
		if (cb)
			std::atomic_store(&dsp_tick_callback_ptr_, std::make_shared<TickDspCallback>(std::move(cb)));
		else
			std::atomic_store(&dsp_tick_callback_ptr_, std::shared_ptr<TickDspCallback>());
	}

	void setDspInputReader(DspInputReader cb) {
		if (cb)
			std::atomic_store(&dsp_input_reader_ptr_, std::make_shared<DspInputReader>(std::move(cb)));
		else
			std::atomic_store(&dsp_input_reader_ptr_, std::shared_ptr<DspInputReader>());
	}

	void setDspOutputWriter(DspOutputWriter cb) {
		if (cb)
			std::atomic_store(&dsp_output_writer_ptr_, std::make_shared<DspOutputWriter>(std::move(cb)));
		else
			std::atomic_store(&dsp_output_writer_ptr_, std::shared_ptr<DspOutputWriter>());
	}

	void setUiResetCallback(UiResetCallback cb) {
		if (cb)
			std::atomic_store(&ui_reset_callback_ptr_, std::make_shared<UiResetCallback>(std::move(cb)));
		else
			std::atomic_store(&ui_reset_callback_ptr_, std::shared_ptr<UiResetCallback>());
	}

	// Write output via the registered DspOutputWriter (safe to call from C callbacks)
	void writeDspOutput(size_t port_num, float value);

	// Read input via the registered DspInputReader (safe to call from C callbacks)
	float readDspInput(size_t port_num) const;

	Usz getTickNumber() const { return tick_number_.load(); }
	void resetTickNumber() { tick_number_.store(0); }

	// Trigger the tick callback immediately without advancing the simulation.
	// Useful to request a UI redraw after manual edits to the field.
	void notifyTick();

	// Reset simulator state to an empty field (keeps current dimensions).
	// Clears undo history, event list, scratch buffer, and requests a UI redraw.
	void reset();

	// Get event list from last step (for processing MIDI events). Lock-free read.
	Oevent_list const* getEvents() const { return &oevent_list_; }
	Usz getEventCount() const { return oevent_list_.count; }

	// Serialization (called from non-DSP thread, can use simple protection)
	json_t* toJson() const;
	void fromJson(json_t* rootJ);

	// Get display buffer dimensions (no longer uses indices with single buffer)
	void getDisplayBuffer(Usz& height, Usz& width) const;
	// Get pointer to field buffer
	Glyph const* getFieldBuffer() const;
	Mark const* getMbufBuffer() const;

	// Helpers for field management
	void setFieldSize(Usz height, Usz width);
	Usz getFieldHeight() const;
	Usz getFieldWidth() const;

	// Undo support
	void setUndoLimit(Usz limit); // 0 disables undo
	bool pushUndo(); // push current field/tick onto undo stack
	bool canUndo() const;
	Usz getUndoCount() const;
	void undo(Glyph* redoBuf);
	// Redo support
	bool canRedo() const { return !redo_history_.empty(); }
	Usz getRedoCount() const { return (Usz)redo_history_.size(); }
	void redo(Glyph* undoBuf);
	void resetUndo();

	bool cutRect(Usz y, Usz x, Usz h, Usz w);
	bool fillRect(Usz y, Usz x, Usz h, Usz w, Glyph fill);
	// Move a rectangular region by placing its contents at the specified destination
	// coordinates. The move is performed atomically with a single undo snapshot.
	// dest_y/dest_x are signed to allow negative moves which will be clipped.
	bool moveRect(Usz y, Usz x, Usz h, Usz w, Isz dest_y, Isz dest_x);

	// Helpers to apply specific commands on the DSP/write buffer (called from `process`).
	void setGlyph(Usz y, Usz x, Glyph g, Mark_flags flags);
	// Paste a raw cells buffer (height h, width w) into the field at dest coords (clips to field bounds)
	bool pasteCells(Glyph* cells, Usz h, Usz w, Usz dest_y, Usz dest_x);

	// Helper request APIs for other UI operations (enqueue commands)
	void setGlyphRequest(Usz y, Usz x, Glyph g, Mark_flags flags = Mark_flag_input, bool doUndo = true);

	void setFieldSizeRequest(Usz h, Usz w, bool doUndo);
	void cutRectRequest(Usz y, Usz x, Usz h, Usz w);
	void fillRectRequest(Usz y, Usz x, Usz h, Usz w, Glyph fill = '.');
	void moveRectRequest(Usz y, Usz x, Usz h, Usz w, Isz dest_y, Isz dest_x);
	void undoRequest();
	void redoRequest();
	void resetRequest();

	// UDP destination persisted in sim
	void setUdpDestination(const std::string& address, const std::string& port);
	std::string getUdpAddress() const { return udpAddress_; }
	std::string getUdpPort() const { return udpPort_; }

	// OSC destination persisted in sim
	void setOscDestination(const std::string& address, const std::string& port);
	std::string getOscAddress() const { return oscAddress_; }
	std::string getOscPort() const { return oscPort_; }

private:
	Field field_;
	Mbuf_reusable mbuf_;

	Oevent_list oevent_list_;
	std::atomic<Usz> tick_number_;
	Usz random_seed_;

	// Tick callback stored as a shared_ptr. We use the free functions
	// std::atomic_load / std::atomic_store for atomic access without needing
	// an std::atomic wrapper (these overloads are provided for shared_ptr).
	std::shared_ptr<UiTickCallback> ui_tick_callback_ptr_;
	// Reset callback (stored atomically as shared_ptr)
	std::shared_ptr<UiResetCallback> ui_reset_callback_ptr_;
	// Callback for into DSP class
	std::shared_ptr<TickDspCallback> dsp_tick_callback_ptr_;
	// Input reader callback (stored atomically as shared_ptr)
	std::shared_ptr<DspInputReader> dsp_input_reader_ptr_;
	// Output writer callback (stored atomically as shared_ptr)
	std::shared_ptr<DspOutputWriter> dsp_output_writer_ptr_;

	// Undo / Redo history
	struct UndoNode { Field f; Usz tick; };
	std::deque<UndoNode> undo_history_;
	std::deque<UndoNode> redo_history_;
	Usz undo_limit_ = 30;


	// Command types for operations requested by the UI thread
	enum class UiCommandType : uint8_t {
		APPLY_GLYPH,
		SET_FIELD_SIZE,
		REPLACE_FIELD,
		CUT_RECT,
		FILL_RECT,
		MOVE_RECT,
		PASTE_CELLS,
		UNDO,
		REDO,
		STEP,
		RESET
	};

	struct UiCommand {
		UiCommandType type;
		// payload
		Usz y = 0, x = 0, h = 0, w = 0;
		Glyph g = '.';
		Mark_flags flags;
		Usz new_h = 0, new_w = 0; // for SetFieldSize/ReplaceField (or dest coords for moves)
		Glyph* cells = nullptr; // heap-allocated buffer for ReplaceField or PASTE_CELLS (height*width)
		UiCommand() : type(UiCommandType::RESET), flags(Mark_flag_none) {}
		~UiCommand() { if (cells) free(cells); }
	};

	// Single-producer/single-consumer queue for UI->DSP commands. We use
	// Rack's `rack::dsp::RingBuffer` implementation.
	rack::dsp::RingBuffer<UiCommand*, 512> ui_cmd_queue_;

	// Request made by UI to force a display publish before the next step.
	// Set by `notifyTick()` on UI thread, processed by `process()` on DSP thread.
	std::atomic<bool> pending_ui_update_{false};

	// UDP device and destination
	Oosc_dev* udp_dev_ = nullptr;
	// Ensure UDP device exists (tries to create with given address/port if missing).
	bool ensureUdpDev(const char* addr, const char* port);
	// Send a list/array of 32-bit integers in OSC format to the configured destination.
	// Destroy UDP device if present.
	void destroyUdpDev();

	std::string udpAddress_ = "127.0.0.1";
	std::string udpPort_ = "49161";
	// Send a raw UDP datagram (no-op if device creation fails).
	void sendUdpDatagram(const char* data, Usz size);

	std::string oscAddress_ = "127.0.0.1";
	std::string oscPort_ = "49162";
	// OSC path prefix used as base when building addresses from glyphs (e.g. "/OSC_MIDI_0/MIDI").
	void sendOscInts(const char* osc_path, I32 const* vals, Usz count);

	// Concurrency model:
	// - DSP thread (process): calls step() lock-free, writes to double buffers
	// - UI thread: reads display data lock-free, requests edits
	// - No synchronization needed between threads - double buffering prevents conflicts
};

} // namespace Ahab
} // namespace StoermelderPackOne