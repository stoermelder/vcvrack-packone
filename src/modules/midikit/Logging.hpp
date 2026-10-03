#pragma once
#include "../../plugin.hpp"
#include "../../components/LedTextField.hpp"
#include "../../vcv/ui.hpp"
#include <rigtorp/MPMCQueue.h>
#include <atomic>
#include <cinttypes>
#include <list>
#include <string>
#include <tuple>

namespace StoermelderPackOne {
namespace MidiKit {

enum class LOG_FORMAT {
	RESET,
	TIMESTAMP,
	INDENTED,
	TEXT
};

// What the log display puts in front of a timestamped line.
enum class LOG_TIME {
	TIMESTAMP,   // seconds since the script was loaded
	FRAME,       // engine frame, as in MIDI-MON
	OFF
};


// Script log + overlay, one per module
// Everything the module tells the widget about: the runtime log and the
// current overlay message. Touched from three threads, which is why the
// contract is stated here once rather than inferred from call sites:
//   - worker thread: writeLog()/writeOverlay() produce log entries + overlay;
//   - audio thread: raises notices (ScriptLog::raise), which become log lines
//     when the log is drained;
//   - loadScript()/onReset() callers produce RESET markers and "No script";
//   - UI thread: the widget drains the log (ScriptLog::tryPop) and the
//     overlay ring (nextOverlayMessageId/getOverlayMessage).
// midiLogMessages is an MPMC queue because the log has concurrent producers;
// overlayQueue is a single-producer ring (worker) drained by the widget.
struct ScriptLog {
	// Log entries, FIFO. MPMC: pushed by the worker (writeLog) and the
	// loadScript/onReset callers (and the drain, for raised notices).
	// (format, seconds since the script loaded, text, engine frame)
	using Entry = std::tuple<LOG_FORMAT, float, std::string, int64_t>;
	rigtorp::MPMCQueue<Entry> midiLogMessages{512};

	// Overlay ring + current message. Single-producer (worker via writeOverlay),
	// single-consumer (widget).
	dsp::RingBuffer<int, 8> overlayQueue;
	std::tuple<std::string, std::string, std::string> overlayMessage;

	// Notices raised from the audio thread. Building a log line allocates, which
	// the audio thread must not do, so it only raises the notice: a flag set,
	// nothing else. The text is a std::string made once, off the audio thread
	// (in the constructor), and becomes a log line when tryPop() next runs on
	// the thread that drains the log. Repeats before that are one line.
	enum Notice {
		OUTPUT_QUEUE_FULL,
		SCHEDULE_QUEUE_FULL,
		TRIGGER_QUEUE_FULL,
		INPUT_QUEUE_FULL,
		TIPSY_INPUT_MALFORMED,
		TIPSY_INPUT_QUEUE_FULL,
		TIMING_LATE,
		NOTICE_COUNT
	};
	struct NoticeSlot {
		std::atomic<bool> pending{false};
		std::string text;
	};
	NoticeSlot notices[NOTICE_COUNT];

	ScriptLog() {
		notices[OUTPUT_QUEUE_FULL].text = "MIDI output queue full, message(s) dropped";
		notices[SCHEDULE_QUEUE_FULL].text = "MIDI schedule queue full, message(s) sent at once";
		notices[TRIGGER_QUEUE_FULL].text = "Trigger output queue full, write(s) dropped";
		notices[INPUT_QUEUE_FULL].text = "MIDI input queue full, message(s) dropped";
		notices[TIPSY_INPUT_MALFORMED].text = "Tipsy input: malformed stream";
		notices[TIPSY_INPUT_QUEUE_FULL].text = "Tipsy input queue full, message(s) dropped";
		notices[TIMING_LATE].text = "Timing: message(s) reached the output too late";
	}

	// Any thread, allocation-free.
	void raise(Notice n) {
		notices[n].pending.store(true, std::memory_order_release);
	}

	// Worker side — writeLog(). Enqueues one entry.
	void push(LOG_FORMAT format, float timestamp, const std::string& text, int64_t frame = 0) {
		midiLogMessages.try_push(std::make_tuple(format, timestamp, text, frame));
	}

	// Takes the next log entry, after turning the notices raised since the last
	// call into lines. For the thread that drains the log (the widget; tests).
	bool tryPop(Entry& entry) {
		for (int i = 0; i < NOTICE_COUNT; i++) {
			if (notices[i].pending.exchange(false, std::memory_order_acquire)) pushText(notices[i].text);
		}
		return midiLogMessages.try_pop(entry);
	}

	// Plain text line. The timestamp is not displayed.
	void pushText(const std::string& text, float timestamp = 0.f) {
		push(LOG_FORMAT::TEXT, timestamp, text);
	}

	// Line prefixed with the timestamp (seconds since the script loaded) or the
	// engine frame, as the display is set to.
	void pushTimestamped(float timestamp, const std::string& text, int64_t frame = 0) {
		push(LOG_FORMAT::TIMESTAMP, timestamp, text, frame);
	}

	// Marks a script load/reset; the widget clears its display on it.
	void pushReset() {
		push(LOG_FORMAT::RESET, 0.f, std::string(""));
	}

	// Worker side — writeOverlay(). Marks one overlay slot with the current
	// message.
	void pushOverlay(const std::string& s1, const std::string& s2, const std::string& s3) {
		overlayQueue.push(0);
		overlayMessage = std::make_tuple(s1, s2, s3);
	}
};


// One log entry as a display line (without a trailing newline); empty for
// entries that print nothing (RESET).
static std::string formatLogEntry(const ScriptLog::Entry& s, LOG_TIME time = LOG_TIME::TIMESTAMP) {
	const std::string& text = std::get<2>(s);
	switch (std::get<0>(s)) {
		case LOG_FORMAT::TIMESTAMP:
			switch (time) {
				case LOG_TIME::FRAME: return string::f("[%9" PRId64 "] %s", std::get<3>(s), text.c_str());
				case LOG_TIME::OFF: return text;
				default: return string::f("[%9.4f] %s", std::get<1>(s), text.c_str());
			}
		case LOG_FORMAT::TEXT:
			return text;
		case LOG_FORMAT::INDENTED:
			return "     " + text;
		default:
			return "";
	}
}


struct LogDisplay : LedTextDisplay {
	std::list<ScriptLog::Entry>* buffer;
	// Owned by the module; null shows timestamps.
	LOG_TIME* logTime = nullptr;
	bool dirty = true;

	LOG_TIME time() const {
		return logTime ? *logTime : LOG_TIME::TIMESTAMP;
	}
	// Set by the widget: adds the running script's section (engine, RAM usage,
	// rack.registerContextMenu items, ...) to the top of this menu and returns
	// whether it added anything. Kept as a hook because that needs the module,
	// which the display knows nothing about.
	std::function<bool(Menu*)> appendScriptItems;

	LogDisplay() {
		color = nvgRGB(0xf0, 0xf0, 0xf0);
		bgColor.a = 0.f;
		fontSize = 9.2f;
		textOffset.y += 2.f;
	}

	void step() override {
		LedTextDisplay::step();
		if (dirty) {
			text = "";
			// Cap to the number of lines that vertically fit.
			size_t size = std::min(buffer->size(), static_cast<size_t>(box.size.y / fontSize) + 1);
			size_t i = 0;
			for (const auto& s : *buffer) {
				if (i >= size) break;
				if (std::get<0>(s) == LOG_FORMAT::RESET) continue;
				text += formatLogEntry(s, time()) + "\n";
				i++;
			}
			dirty = false;
		}
	}

	void reset() {
		buffer->clear();
		dirty = true;
	}

	// The whole buffer as text, oldest line first (the display itself shows the
	// newest first, capped to what fits).
	std::string toText() const {
		std::string out;
		for (auto it = buffer->rbegin(); it != buffer->rend(); ++it) {
			if (std::get<0>(*it) == LOG_FORMAT::RESET) continue;
			out += formatLogEntry(*it, time()) + "\n";
		}
		return out;
	}

	void appendContextMenu(Menu* menu) {
		bool empty = buffer->empty();
		if (appendScriptItems && appendScriptItems(menu)) menu->addChild(new MenuSeparator());
		menu->addChild(createMenuLabel("Log"));
		if (logTime) {
			menu->addChild(StoermelderPackOne::Rack::createMapSubmenuItem<LOG_TIME>("Timestamp",
				{
					{ LOG_TIME::TIMESTAMP, "Seconds" },
					{ LOG_TIME::FRAME, "Engine frame" },
					{ LOG_TIME::OFF, "Off" }
				},
				[=]() { return *logTime; },
				[=](LOG_TIME mode) {
					*logTime = mode;
					dirty = true;
				}
			));
		}
		menu->addChild(createMenuItem("Copy to clipboard", "", [=]() {
			StoermelderPackOne::vcv::ui::setClipboard(toText());
		}, empty));
		menu->addChild(createMenuItem("Clear", "", [=]() {
			reset();
		}, empty));
	}

	void onButton(const ButtonEvent& e) override {
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_RIGHT) {
			appendContextMenu(createMenu());
			e.consume(this);
			return;
		}
		LedTextDisplay::onButton(e);
	}
};

} // namespace MidiKit
} // namespace StoermelderPackOne
