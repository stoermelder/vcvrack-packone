#include "MidiTrackingProcessor.hpp"
#include <algorithm>

namespace StoermelderPackOne {

template<uint16_t MAPCOUNT>
MidiTrackingProcessor<MAPCOUNT>::MidiTrackingProcessor() {
	midiProcessor.subscribe(this);
	clearMaps();
}

template<uint16_t MAPCOUNT>
rack::midi::InputQueue& MidiTrackingProcessor<MAPCOUNT>::getInput() {
	return midiProcessor.getInput();
}

template<uint16_t MAPCOUNT>
void MidiTrackingProcessor<MAPCOUNT>::process(int64_t frame) {
	midiProcessor.process(frame);
}

template<uint16_t MAPCOUNT>
void MidiTrackingProcessor<MAPCOUNT>::enableCc() {
	mapCc.resize(128, {});
}

template<uint16_t MAPCOUNT>
void MidiTrackingProcessor<MAPCOUNT>::enableNotes() {
	mapNote.resize(128, {});
}

template<uint16_t MAPCOUNT>
bool MidiTrackingProcessor<MAPCOUNT>::processMidi(const MessageEx& msg) {
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

template<uint16_t MAPCOUNT>
void MidiTrackingProcessor<MAPCOUNT>::processNoteOn(const MessageEx& msg) {
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

template<uint16_t MAPCOUNT>
void MidiTrackingProcessor<MAPCOUNT>::processNoteOff(const MessageEx& msg) {
	uint8_t note = msg.getNote();
	for (uint16_t id : mapNote[note]) {
		if (handler) handler->processMapUpdate(MidiTrackingType::NOTE, id, 0);
	}
}

template<uint16_t MAPCOUNT>
void MidiTrackingProcessor<MAPCOUNT>::processCc(const MessageEx& msg) {
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

template<uint16_t MAPCOUNT>
void MidiTrackingProcessor<MAPCOUNT>::setMap(MidiTrackingType type, uint16_t mapId, uint16_t param) {
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

template<uint16_t MAPCOUNT>
bool MidiTrackingProcessor<MAPCOUNT>::getMapLearn() {
	return learnActive;
}

template<uint16_t MAPCOUNT>
void MidiTrackingProcessor<MAPCOUNT>::enableMapLearn(uint16_t mapId) {
	learnActive = true;
	learnId = mapId;
}

template<uint16_t MAPCOUNT>
void MidiTrackingProcessor<MAPCOUNT>::disableMapLearn() {
	learnActive = false;
}

template<uint16_t MAPCOUNT>
void MidiTrackingProcessor<MAPCOUNT>::disableMapLearn(uint16_t mapId) {
	if (mapId == learnId) learnActive = false;
}

template<uint16_t MAPCOUNT>
void MidiTrackingProcessor<MAPCOUNT>::clearMaps() {
	for (uint16_t i = 0; i < MAPCOUNT; i++) {
		clearMap(i);
	}
}

template<uint16_t MAPCOUNT>
void MidiTrackingProcessor<MAPCOUNT>::clearMap(uint16_t mapId) {
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

template<uint16_t MAPCOUNT>
const typename MidiTrackingProcessor<MAPCOUNT>::RevMap& MidiTrackingProcessor<MAPCOUNT>::getMap(uint16_t learnId) {
	return revMap[learnId];
}

template<uint16_t MAPCOUNT>
json_t* MidiTrackingProcessor<MAPCOUNT>::dataToJson() {
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

template<uint16_t MAPCOUNT>
void MidiTrackingProcessor<MAPCOUNT>::dataFromJson(json_t* rootJ) {
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

} // namespace StoermelderPackOne


// Explicit template instantiation for 19 maps
template struct StoermelderPackOne::MidiTrackingProcessor<19>;
template struct StoermelderPackOne::MidiTrackingProcessor<64>;