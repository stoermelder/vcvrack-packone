#include "rack.hpp"


using namespace rack;

struct PolyLedWidget : Widget {
    PolyLedWidget();
    void setModule(Module *module, int firstlightId);
};