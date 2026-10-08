#pragma once

// Shared preamble for the MIDICAT test suite.
// Included by MidiCat.test.cpp, which pulls the test cases in from MidiCat.test.module.hpp
// inside their own namespace.

#include "../../test/framework.hpp"
#include "MidiCat.hpp"
#include "MidiCat.cpp"
#include "../midi/MidiTrackingProcessor.hpp"

using namespace StoermelderPackOne;
using namespace StoermelderPackOne::MidiCat;

// A UiAccess mock that records saveDialog and openDialog calls and returns scripted answers.
struct MockUiAccess : StoermelderPackOne::vcv::UiAccess {
	struct SaveCall { std::string filters, dir, filename; };
	std::vector<SaveCall> saveCalls;
	std::vector<std::string> saveResults;  // queue consumed in order
	int saveIndex = 0;

	struct OpenCall { std::string filters, dir; };
	std::vector<OpenCall> openCalls;
	std::vector<std::string> openResults;  // queue consumed in order
	int openIndex = 0;

	struct Message { vcv::MessageType type; vcv::MessageButtons buttons; std::string msg; };
	std::vector<Message> messages;

	std::string saveDialog(const std::string& filters, const std::string& dir, const std::string& filename) override {
		saveCalls.push_back({filters, dir, filename});
		if (saveIndex < (int) saveResults.size()) return saveResults[saveIndex++];
		return "";
	}

	std::string openDialog(const std::string& filters, const std::string& dir) override {
		openCalls.push_back({filters, dir});
		if (openIndex < (int) openResults.size()) return openResults[openIndex++];
		return "";  // cancelled by default
	}

	bool message(vcv::MessageType type, vcv::MessageButtons buttons, const std::string& msg) override {
		messages.push_back({type, buttons, msg});
		return true;
	}
};

// A FileAccess mock that records read()/write() calls and returns scripted content.
struct MockFileAccess : StoermelderPackOne::vcv::FileAccess {
	struct ReadCall { std::string path; };
	std::vector<ReadCall> reads;
	mutable std::vector<std::string> readResults;  // queue consumed in order
	mutable int readIndex = 0;

	struct WriteCall { std::string path, data; };
	std::vector<WriteCall> writes;
	bool failWrites = false;

	bool read(const std::string& path, std::string& data) const override {
		const_cast<MockFileAccess*>(this)->reads.push_back({path});
		if (readIndex < (int) readResults.size()) {
			data = readResults[readIndex++];
			return true;
		}
		return false;
	}

	bool write(const std::string& path, const std::string& data) override {
		if (failWrites) return false;
		writes.push_back({path, data});
		return true;
	}
};

// A recording HistoryAccess. Owns the actions it records (push takes ownership).
struct MockHistoryAccess : StoermelderPackOne::vcv::HistoryAccess {
	std::vector<::rack::history::Action*> pushed;
	void push(::rack::history::Action* a) override { pushed.push_back(a); }
	~MockHistoryAccess() { for (auto* a : pushed) delete a; }
};

Test::TestContext<> testContext;

// Helper class to provide a test module with parameters
struct TestModule : rack::Module {
	enum ParamIds {
		TEST_PARAM_1,
		TEST_PARAM_2,
		TEST_PARAM_3,
		TEST_PARAM_4,
		TEST_PARAM_5,
		NUM_PARAMS
	};

	TestModule() {
		config(NUM_PARAMS, 0, 0, 0);
		ParamQuantity* pq;
		configParam(TEST_PARAM_1, 0.f, 1.f, 0.5f, "Test Parameter 1");
		configParam(TEST_PARAM_2, 0.f, 127.f, 0.f, "Test Parameter 2");
		configParam(TEST_PARAM_3, -10.f, 10.f, 0.f, "Test Parameter 3");
		pq = configParam(TEST_PARAM_4, 0.f, 10.f, 0.f, "Test Parameter 4 (Snapped)");
		pq->snapEnabled = true;
		pq = configParam(TEST_PARAM_5, 0.f, 700.f, 0.f, "Test Parameter 5 (Snapped)");
		pq->snapEnabled = true;
	}
};