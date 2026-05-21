#pragma once
#include "MidiProcessor.hpp"
#include <algorithm>

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

	MidiTrackingProcessor() {
		midiProcessor.subscribe(this);
		clearMaps();
	}

	rack::midi::InputQueue& getInput() {
		return midiProcessor.getInput();
	}

	void process(int64_t frame) {
		midiProcessor.process(frame);
	}

	void enableCc() {
		mapCc.resize(128, {});
	}

	void enableNotes() {
		mapNote.resize(128, {});
	}

	bool processMidi(const MessageEx& msg) override {
		switch (msg.type) {
			case MessageEx::Type::CC:
				if (mapCc.size() > 0) processCc(msg);
				break;
			case MessageEx::Type::NOTE_ON:
				if (msg.getValue() > 0) {
					if (mapNote.size() > 0) processNoteOn(msg);
				}
				else {
					if (mapNote.size() > 0) processNoteOff(msg);
				}
				break;
			case MessageEx::Type::NOTE_OFF:
				if (mapNote.size() > 0) processNoteOff(msg);
				break;
			default:
				break;
		}
		return true;
	}

	void processNoteOn(const MessageEx& msg) {
		uint8_t note = msg.getNote();
		uint8_t vel = msg.getValue();
		if (learnActive && vel > 0) {
			clearMap(learnId);
			setMap(MidiTrackingType::NOTE, learnId, note);
			disableMapLearn();
			if (handler) handler->processMapLearn(MidiTrackingType::NOTE, learnId);
			return;
		}
		for (uint16_t id : mapNote[note]) {
			if (handler) handler->processMapUpdate(MidiTrackingType::NOTE, id, vel);
		}
	}

	void processNoteOff(const MessageEx& msg) {
		uint8_t note = msg.getNote();
		for (uint16_t id : mapNote[note]) {
			if (handler) handler->processMapUpdate(MidiTrackingType::NOTE, id, 0);
		}
	}

	void processCc(const MessageEx& msg) {
		uint8_t cc = msg.getNote();
		uint8_t value = msg.getValue();
		if (learnActive && value > 0) {
			clearMap(learnId);
			setMap(MidiTrackingType::CC, learnId, cc);
			disableMapLearn();
			if (handler) handler->processMapLearn(MidiTrackingType::CC, learnId);
			return;
		}
		for (uint16_t id : mapCc[cc]) {
			if (handler) handler->processMapUpdate(MidiTrackingType::CC, id, value);
		}
	}

	void setMap(MidiTrackingType type, uint16_t mapId, uint16_t param) {
		revMap[mapId].type = type;
		revMap[mapId].param = param;
		switch (type) {
			case MidiTrackingType::CC:
				mapCc[param].push_back(mapId);
				break;
			case MidiTrackingType::NOTE:
				mapNote[param].push_back(mapId);
				break;
			default:
				break;
		}
	}

	bool getMapLearn() {
		return learnActive;
	}

	void enableMapLearn(uint16_t mapId) {
		learnActive = true;
		learnId = mapId;
	}

	void disableMapLearn() {
		learnActive = false;
	}

	void disableMapLearn(uint16_t mapId) {
		if (mapId == learnId) learnActive = false;
	}

	void clearMaps() {
		for (uint16_t i = 0; i < MAPCOUNT; i++) {
			clearMap(i);
		}
	}

	void clearMap(uint16_t mapId) {
		auto& t = revMap[mapId];
		switch (t.type) {
			case MidiTrackingType::NOTE:
				if (mapNote.size() > 0) {
					auto it = std::find(mapNote[t.param].begin(), mapNote[t.param].end(), mapId);
					if (it != mapNote[t.param].end()) mapNote[t.param].erase(it);
				}
				break;
			case MidiTrackingType::CC:
				if (mapCc.size() > 0) {
					auto it = std::find(mapCc[t.param].begin(), mapCc[t.param].end(), mapId);
					if (it != mapCc[t.param].end()) mapCc[t.param].erase(it);
				}
				break;
			default:
				break;
		}
		t.type = MidiTrackingType::NONE;
		t.param = 0;
	}

	const RevMap& getMap(uint16_t learnId) {
		return revMap[learnId];
	}

	json_t* dataToJson() {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "midiInput", getInput().toJson());

		json_t* mapsJ = json_array();
		for (uint16_t i = 0; i < MAPCOUNT; i++) {
			json_t* mapJ = json_object();
			json_object_set_new(mapJ, "type", json_integer((int)revMap[i].type));
			json_object_set_new(mapJ, "param", json_integer(revMap[i].param));
			json_array_append_new(mapsJ, mapJ);
		}
		json_object_set_new(rootJ, "revMap", mapsJ);

		return rootJ;
	}

	void dataFromJson(json_t* rootJ) {
		json_t* midiInputJ = json_object_get(rootJ, "midiInput");
		getInput().fromJson(midiInputJ);

		json_t* mapsJ = json_object_get(rootJ, "revMap");
		json_t* mapJ;
		size_t i;
		json_array_foreach(mapsJ, i, mapJ) {
			MidiTrackingType type = (MidiTrackingType)json_integer_value(json_object_get(mapJ, "type"));
			uint16_t param = json_integer_value(json_object_get(mapJ, "param"));
			setMap(type, i, param);
		}
	}
};

} // namespace StoermelderPackOne