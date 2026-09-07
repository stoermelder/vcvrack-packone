#pragma once

// Shared preamble and helpers for the Arena test suite.
// Included by every Arena*.test.cpp file; each test file builds its own
// binary (see plugin-test.mk), so the test context is per-binary.

#include "../../test/framework.hpp"
#include "Arena.cpp"

using namespace StoermelderPackOne::Arena;

typedef ArenaModule<8, 4> MODULE;
static const int IN_PORTS = 8;
static const int MIX_PORTS = 4;

SYNC_MODEL(modelArena, "Arena");
Test::TestContext<> testContext;


// Helpers

// Set the effective position of an IN port by loading filters and params directly.
static void setInPosition(MODULE* m, int j, float x, float y) {
	m->nodes.uiX[j] = x;
	m->nodes.xFilter[j].out = x;
	m->nodes.uiY[j] = y;
	m->nodes.yFilter[j].out = y;
	m->params[MODULE::IN_X_POS + j].setValue(x);
	m->params[MODULE::IN_Y_POS + j].setValue(y);
}

// Set the effective position of a MIX port.
static void setMixPosition(MODULE* m, int i, float x, float y) {
	m->mixUiX[i] = x;
	m->mixXfilter[i].out = x;
	m->mixUiY[i] = y;
	m->mixYfilter[i].out = y;
	m->params[MODULE::MIX_X_POS + i].setValue(x);
	m->params[MODULE::MIX_Y_POS + i].setValue(y);
}

// Set an IN port's radius filter and stored value.
static void setRadius(MODULE* m, int j, float r) {
	m->nodes.radiusUi[j] = r;
	m->nodes.radiusFilter[j].out = r;
}

// Build a single sequence for a MIX port's currently-selected slot from parallel x/y arrays.
static void setSeqData(MODULE* m, int port, const std::vector<float>& xs, const std::vector<float>& ys) {
	auto& item = m->seqData[port][m->seqSelected[port]];
	item.length = (int)xs.size();
	for (size_t i = 0; i < xs.size(); i++) {
		item.x[i] = xs[i];
		item.y[i] = ys[i];
	}
}

// Mark a specific slot non-empty (one point is enough — seqProcess's trigger
// modes only ever consult .length, never the point data itself).
static void markSlotUsed(MODULE* m, int port, int slot) {
	auto& item = m->seqData[port][slot];
	item.length = 1;
	item.x[0] = 0.f;
	item.y[0] = 0.f;
}
