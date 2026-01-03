#pragma once
#include "MidiProcessor.hpp"

namespace StoermelderPackOne {

enum class MidiTrackingType {
	NONE,
	NOTE,
	CC
};

struct MidiTrackingProcessorHandler {
	virtual void processMapLearn(MidiTrackingType type, uint16_t mapId) {}
	virtual void processMapUpdate(MidiTrackingType type, uint16_t mapId, uint16_t value) {}
};

template<uint16_t MAPCOUNT>
struct MidiTrackingProcessor : MidiProcessorHandler {
	// Public members (intentionally public so tests and other modules can access without
	// relying on private internals).
	std::vector<std::vector<uint16_t>> mapCc;
	std::vector<std::vector<uint16_t>> mapNote;

	struct RevMap {
		MidiTrackingType type;
		uint16_t param;
	};

	RevMap revMap[MAPCOUNT];

	uint16_t learnId;
	bool learnActive = false;

	MidiProcessor midiProcessor;
	MidiTrackingProcessorHandler* handler = nullptr;

	MidiTrackingProcessor();

	// Access to the underlying midi input queue
	rack::midi::InputQueue& getInput();

	// Process pending midi messages (forward to internal MidiProcessor)
	void process(int64_t frame);

	void enableCc();
	void enableNotes();

	// Midi processor callback
	bool processMidi(const MessageEx& msg) override;

	void processNoteOn(const MessageEx& msg);
	void processNoteOff(const MessageEx& msg);
	void processCc(const MessageEx& msg);

	void setMap(MidiTrackingType type, uint16_t mapId, uint16_t param);

	bool getMapLearn();
	void enableMapLearn(uint16_t mapId);
	void disableMapLearn();
	void disableMapLearn(uint16_t mapId);

	void clearMaps();
	void clearMap(uint16_t mapId);

	const RevMap& getMap(uint16_t learnId);

	json_t* dataToJson();
	void dataFromJson(json_t* rootJ);
};

} // namespace StoermelderPackOne