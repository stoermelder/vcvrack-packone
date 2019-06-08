#include "widgets.hpp"


PolyLedWidget::PolyLedWidget() {
    box.size = mm2px(Vec(6.f, 6.f));
}

void PolyLedWidget::setModule(Module *module, int firstlightId) {
    addChild(createLightCentered<TinyLight<BlueLight>>(mm2px(Vec(0, 0)), module, firstlightId + 0));
    addChild(createLightCentered<TinyLight<BlueLight>>(mm2px(Vec(2, 0)), module, firstlightId + 1));
    addChild(createLightCentered<TinyLight<BlueLight>>(mm2px(Vec(4, 0)), module, firstlightId + 2));
    addChild(createLightCentered<TinyLight<BlueLight>>(mm2px(Vec(6, 0)), module, firstlightId + 3));
    addChild(createLightCentered<TinyLight<BlueLight>>(mm2px(Vec(0, 2)), module, firstlightId + 4));
    addChild(createLightCentered<TinyLight<BlueLight>>(mm2px(Vec(2, 2)), module, firstlightId + 5));
    addChild(createLightCentered<TinyLight<BlueLight>>(mm2px(Vec(4, 2)), module, firstlightId + 6));
    addChild(createLightCentered<TinyLight<BlueLight>>(mm2px(Vec(6, 2)), module, firstlightId + 7));
    addChild(createLightCentered<TinyLight<BlueLight>>(mm2px(Vec(0, 4)), module, firstlightId + 8));
    addChild(createLightCentered<TinyLight<BlueLight>>(mm2px(Vec(2, 4)), module, firstlightId + 9));
    addChild(createLightCentered<TinyLight<BlueLight>>(mm2px(Vec(4, 4)), module, firstlightId + 10));
    addChild(createLightCentered<TinyLight<BlueLight>>(mm2px(Vec(6, 4)), module, firstlightId + 11));
    addChild(createLightCentered<TinyLight<BlueLight>>(mm2px(Vec(0, 6)), module, firstlightId + 12));
    addChild(createLightCentered<TinyLight<BlueLight>>(mm2px(Vec(2, 6)), module, firstlightId + 13));
    addChild(createLightCentered<TinyLight<BlueLight>>(mm2px(Vec(4, 6)), module, firstlightId + 14));
    addChild(createLightCentered<TinyLight<BlueLight>>(mm2px(Vec(6, 6)), module, firstlightId + 15));
}