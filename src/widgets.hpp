#pragma once
#include "rack.hpp"

using namespace rack;

template < typename LIGHT = BlueLight, int COLORS = 1>
struct PolyLedWidget : Widget {
	PolyLedWidget() {
		box.size = mm2px(Vec(6.f, 6.f));
	}

	void setModule(Module* module, int firstlightId) {
		addChild(createLightCentered<TinyLight<LIGHT>>(mm2px(Vec(0, 0)), module, firstlightId + COLORS * 0));
		addChild(createLightCentered<TinyLight<LIGHT>>(mm2px(Vec(2, 0)), module, firstlightId + COLORS * 1));
		addChild(createLightCentered<TinyLight<LIGHT>>(mm2px(Vec(4, 0)), module, firstlightId + COLORS * 2));
		addChild(createLightCentered<TinyLight<LIGHT>>(mm2px(Vec(6, 0)), module, firstlightId + COLORS * 3));
		addChild(createLightCentered<TinyLight<LIGHT>>(mm2px(Vec(0, 2)), module, firstlightId + COLORS * 4));
		addChild(createLightCentered<TinyLight<LIGHT>>(mm2px(Vec(2, 2)), module, firstlightId + COLORS * 5));
		addChild(createLightCentered<TinyLight<LIGHT>>(mm2px(Vec(4, 2)), module, firstlightId + COLORS * 6));
		addChild(createLightCentered<TinyLight<LIGHT>>(mm2px(Vec(6, 2)), module, firstlightId + COLORS * 7));
		addChild(createLightCentered<TinyLight<LIGHT>>(mm2px(Vec(0, 4)), module, firstlightId + COLORS * 8));
		addChild(createLightCentered<TinyLight<LIGHT>>(mm2px(Vec(2, 4)), module, firstlightId + COLORS * 9));
		addChild(createLightCentered<TinyLight<LIGHT>>(mm2px(Vec(4, 4)), module, firstlightId + COLORS * 10));
		addChild(createLightCentered<TinyLight<LIGHT>>(mm2px(Vec(6, 4)), module, firstlightId + COLORS * 11));
		addChild(createLightCentered<TinyLight<LIGHT>>(mm2px(Vec(0, 6)), module, firstlightId + COLORS * 12));
		addChild(createLightCentered<TinyLight<LIGHT>>(mm2px(Vec(2, 6)), module, firstlightId + COLORS * 13));
		addChild(createLightCentered<TinyLight<LIGHT>>(mm2px(Vec(4, 6)), module, firstlightId + COLORS * 14));
		addChild(createLightCentered<TinyLight<LIGHT>>(mm2px(Vec(6, 6)), module, firstlightId + COLORS * 15));
	}
};