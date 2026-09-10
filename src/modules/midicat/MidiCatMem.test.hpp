#pragma once

// Shared preamble for the MIDICATMEM test suite.
// Included by MidiCatMem.test.cpp, which pulls the test cases in from MidiCatMem.test.module.hpp
// inside their own namespace.

#include "../../test/framework.hpp"
#include "MidiCatMem.cpp"
#include "MidiCat.cpp"
#include "../midi/MidiTrackingProcessor.hpp"

using namespace StoermelderPackOne::MidiCat;

Test::TestContext<> testContext;

// Helper: set up a CC+param binding on a given MidiCat channel.
// The target module must already be registered in the engine.
static void setupBinding(Test::Harness& h, MidiCatModule* midicat, Module* target, int channel, int cc, int paramId) {
	midicat->processDivider.setDivision(1);
	midicat->enableLearn(channel, true);
	midicat->midiInput.onMessage(Test::makeMidiMessage(0xb, 0, cc, 64));
	midicat->learnParam(channel, target->id, paramId);
	h.dspStep();
	midicat->slots[channel].cc.ccMode = CCMODE::DIRECT;
}

// Helper: build and insert a MemModule entry for `target` in mem->midiMap.
// The paramMap gets one entry: { paramId=pid, cc=cc }.
static void insertMemEntry(MidiCatMemModule* mem, Module* target, int pid, int cc) {
	auto* memMod = new MemModule;
	memMod->pluginName = target->model->plugin->name;
	memMod->moduleName = target->model->name;
	MemParam* p = new MemParam;
	p->paramId = pid;
	p->cc = cc;
	p->ccMode = CCMODE::DIRECT;
	memMod->paramMap.push_back(p);
	auto key = std::make_pair(target->model->plugin->slug, target->model->slug);
	mem->midiMap[key] = memMod;
}