#pragma once
#include <rack.hpp>

namespace StoermelderPackOne {

using namespace rack;

struct CurveMenuItem : MenuItem {
    struct CurveQuantity : Quantity {
        CurveMenuItem* me;
        CurveQuantity(CurveMenuItem* me) {
            this->me = me;
        }
        void setValue(float value) override {
            me->setCurveValue(clamp(value, -3.f, 3.f));
        }
        float getValue() override {
            return me->getCurveValue();
        }
        float getDefaultValue() override {
            return 0.f;
        }
        float getMinValue() override {
            return -3.f;
        }
        float getMaxValue() override {
            return 3.f;
        }
        float getDisplayValue() override {
            return getValue();
        }
        std::string getDisplayValueString() override {
            return string::f("%.2f", me->getCurveValue());
        }
        void setDisplayValue(float displayValue) override {
            setValue(displayValue);
        }
        std::string getLabel() override {
            return "Curve";
        }
        std::string getUnit() override {
            return "";
        }
    };

    static constexpr float SENSITIVITY = 0.001f;
    Quantity* quantity;

    virtual float getCurveValue() { return 0.f; }
    virtual void setCurveValue(float v) { }

    CurveMenuItem() {
        quantity = new CurveQuantity(this);
        box.size.y = BND_WIDGET_HEIGHT;
    }

    ~CurveMenuItem() {
        delete quantity;
    }

    void draw(const DrawArgs& args) override {
        BNDwidgetState state = BND_DEFAULT;
        if (APP->event->hoveredWidget == this)
            state = BND_HOVER;
        if (APP->event->draggedWidget == this)
            state = BND_ACTIVE;

        float progress = quantity ? quantity->getScaledValue() : 0.f;
        std::string text = quantity ? quantity->getString() : "";

        // If parent is a Menu, make corners sharp
        ui::Menu* parentMenu = dynamic_cast<ui::Menu*>(getParent());
        int flags = parentMenu ? BND_CORNER_ALL : BND_CORNER_NONE;
        bndSlider(args.vg, 0.0, 0.0, box.size.x, box.size.y, flags, state, progress, text.c_str(), NULL);
    }

    void onDragDrop(const event::DragDrop& e) override { }

    void onDragStart(const event::DragStart& e) override {
        if (e.button != GLFW_MOUSE_BUTTON_LEFT)
            return;
        APP->window->cursorLock();
    }

    void onDragMove(const event::DragMove& e) override {
        if (quantity) {
            quantity->moveScaledValue(SENSITIVITY * e.mouseDelta.x);
        }
    }

    void onDragEnd(const event::DragEnd& e) override {
        APP->window->cursorUnlock();
    }

    void onDoubleClick(const event::DoubleClick& e) override {
        if (quantity) {
            quantity->reset();
        }
    }

    Menu* createChildMenu() override {
        struct CurveWidget : OpaqueWidget {
            CurveMenuItem* me;

            CurveWidget(CurveMenuItem* me) {
                this->me = me;
            }

            void draw(const DrawArgs& args) override {
                //nvgBeginPath(args.vg);
                nvgStrokeColor(args.vg, rack::color::WHITE);
                //nvgRoundedRect(args.vg, 2.f, 2.f, box.size.x - 4.f, box.size.y - 4.f, 2.f);
                //nvgStroke(args.vg);
                //nvgStrokeWidth(args.vg, 1.0f);

                float maxX = box.size.x;
                float maxY = box.size.y;
                float min = 1.f;
                float max = M_E;
                int steps = 60;

                nvgSave(args.vg);
                nvgBeginPath(args.vg);

                float a_exp2_taylor5 = dsp::exp2_taylor5(me->getCurveValue());
                Rect b = Rect(Vec(0, 2), Vec(maxX, maxY - 4));

                for (int i = 0; i < steps; i++) {
                    float x = float(i) / (steps - 1);
                    float fx = min + (max - min) * x;
                    float fy = std::exp(std::pow(std::log(fx), a_exp2_taylor5));
                    float y = rack::rescale(fy, 1.f, M_E, 0.f, 1.f);

                    float px = b.pos.x + 2.f + (b.size.x - 4.f) * x;
                    float py = b.pos.y + 2.f + (b.size.y - 4.f) * (1.0 - y);
                    if (i == 0)
                        nvgMoveTo(args.vg, px, py);
                    else
                        nvgLineTo(args.vg, px, py);
                }

                nvgLineCap(args.vg, NVG_ROUND);
                nvgMiterLimit(args.vg, 2.0f);
                nvgStrokeWidth(args.vg, 2.0f);
                nvgStroke(args.vg);
                nvgRestore(args.vg);
            }
        };

        const float width = 130.f;
        const float height = 130.f;

        Menu* menu = new Menu;
        CurveWidget* w = new CurveWidget(this);
        w->box.pos = Vec(0.f, 2.f);
        w->box.size = Vec(width, height - 4.f);
        menu->addChild(w);
        return menu;
    }
};

} // namespace StoermelderPackOne