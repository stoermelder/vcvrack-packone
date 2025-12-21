#include "plugin.hpp"
#include <queue>
#include "components/MenuColorLabel.hpp"
#include "components/MenuColorField.hpp"
#include "components/MenuColorPicker.hpp"

namespace StoermelderPackOne {
namespace PanicRoom {

struct PanicRoomModule : Module {
    enum ParamIds {
        NUM_PARAMS
    };
    enum InputIds {
        NUM_INPUTS
    };
    enum OutputIds {
        NUM_OUTPUTS
    };
    enum LightIds {
        NUM_LIGHTS
    };

    /** [Stored to JSON] */
    int panelTheme = 0;

    /** Stored to JSON */
    bool restrictionEnabled = false;
    /** Stored to JSON */
    math::Rect restrictionBox;
    /** [Stored to JSON] */
    NVGcolor outsideColor;
    /** [Stored to JSON] */
    float outsideAlpha = 0.5f;

    PanicRoomModule() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        onReset();
    }

    void onReset() override {
        restrictionEnabled = false;
        restrictionBox = math::Rect();
        outsideColor = nvgRGBAf(0.f, 0.f, 0.f, 1.f);
        outsideAlpha = 0.5f;
    }

    json_t* dataToJson() override {
        json_t* rootJ = json_object();
        json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));
        // restrictionEnabled
        json_object_set_new(rootJ, "restrictionEnabled", json_boolean(restrictionEnabled));
        // restrictionBox
        Vec p = restrictionBox.pos.minus(RACK_OFFSET);
        p = p.div(RACK_GRID_SIZE);
        json_t* restrictionBoxJ = json_array();
        json_array_append_new(restrictionBoxJ, json_real(p.x));
        json_array_append_new(restrictionBoxJ, json_real(p.y));
        json_array_append_new(restrictionBoxJ, json_real(restrictionBox.getWidth()));
        json_array_append_new(restrictionBoxJ, json_real(restrictionBox.getHeight()));
        json_object_set_new(rootJ, "restrictionBox", restrictionBoxJ);
        // outsideColor
        json_object_set_new(rootJ, "outsideColor", json_string(color::toHexString(outsideColor).c_str()));
        // outsideAlpha
        json_object_set_new(rootJ, "outsideAlpha", json_real(outsideAlpha));
        return rootJ;
    }

    void dataFromJson(json_t* rootJ) override {
        panelTheme = json_integer_value(json_object_get(rootJ, "panelTheme"));
        // restrictionEnabled
        json_t* restrictionEnabledJ = json_object_get(rootJ, "restrictionEnabled");
        if (restrictionEnabledJ) {
            restrictionEnabled = json_boolean_value(restrictionEnabledJ);
        }
        // restrictionBox
        json_t* restrictionBoxJ = json_object_get(rootJ, "restrictionBox");
        if (restrictionBoxJ) {
            float x = json_number_value(json_array_get(restrictionBoxJ, 0));
            float y = json_number_value(json_array_get(restrictionBoxJ, 1));
            float width = json_number_value(json_array_get(restrictionBoxJ, 2));
            float height = json_number_value(json_array_get(restrictionBoxJ, 3));
            Vec p = Vec(x, y).mult(RACK_GRID_SIZE);
            p = p.plus(RACK_OFFSET);
            restrictionBox = math::Rect(p, math::Vec(width, height));
        }
        // outsideColor
        json_t* outsideColorJ = json_object_get(rootJ, "outsideColor");
        if (outsideColorJ) {
            outsideColor = color::fromHexString(json_string_value(outsideColorJ));
        }
        // outsideAlpha
        json_t* outsideAlphaJ = json_object_get(rootJ, "outsideAlpha");
        if (outsideAlphaJ) {
            outsideAlpha = json_number_value(outsideAlphaJ);
        }
    }
    };


    struct PanicRoomRestrictionWidget : Widget {
    PanicRoomModule* module;

    bool learnMode = false;
    bool bindLights = false;

    bool selecting = false;
    math::Vec selectionStart;
    math::Vec selectionEnd;
    math::Vec mousePos;

    void enableLearn(bool bindLights = false) {
        learnMode = !learnMode;
        GLFWcursor* cursor = glfwCreateStandardCursor(GLFW_CROSSHAIR_CURSOR);
        glfwSetCursor(APP->window->win, cursor);
    }

    void onHover(const HoverEvent& e) override {
        mousePos = e.pos;
        Widget::onHover(e);
    }

    void onButton(const ButtonEvent& e) override {
        if (learnMode && e.button == GLFW_MOUSE_BUTTON_LEFT) {
            e.consume(this);
        }
        Widget::onButton(e);
    }

    void onDragStart(const DragStartEvent& e) override {
        if (learnMode && e.button == GLFW_MOUSE_BUTTON_LEFT) {
            selecting = true;
            selectionStart = mousePos;
            selectionEnd = mousePos;
            e.consume(this);
        }
        Widget::onDragStart(e);
    }

    void onDragEnd(const DragEndEvent& e) override {
        if (selecting && e.button == GLFW_MOUSE_BUTTON_LEFT) {
            Rect r = math::Rect::fromCorners(selectionStart, selectionEnd);
            Vec pos = ((r.pos - RACK_OFFSET) / RACK_GRID_SIZE).floor();
            pos = pos * RACK_GRID_SIZE + RACK_OFFSET;
            Vec size = ((r.size + r.pos - pos) / RACK_GRID_SIZE).floor() + Vec(0.f, 1.f);
            size = size * RACK_GRID_SIZE;
            module->restrictionBox = math::Rect(pos, size);
            module->restrictionEnabled = true;

            selecting = false;
            learnMode = false;
            glfwSetCursor(APP->window->win, NULL);
            e.consume(this);
        }
        Widget::onDragEnd(e);
    }

    void onDragHover(const DragHoverEvent& e) override {
        mousePos = e.pos;
        if (selecting) {
            selectionEnd = mousePos;
        }
        Widget::onDragHover(e);
    }

    void draw(const DrawArgs& args) override {
        if (selecting) {
            // Draw selection rectangle
            nvgBeginPath(args.vg);
            math::Rect selectionBox = math::Rect::fromCorners(selectionStart, selectionEnd);
            nvgRect(args.vg, RECT_ARGS(selectionBox));
            nvgFillColor(args.vg, nvgRGBAf(0, 0, 1, 0.25));
            nvgFill(args.vg);
            nvgStrokeWidth(args.vg, 2.0);
            nvgStrokeColor(args.vg, nvgRGBAf(0, 0, 1, 0.5));
            nvgStroke(args.vg);
        }

        if (module->restrictionEnabled) {
            auto c = module->outsideColor;
            // Shade everything outside the restrictionBox and outline the box
            nvgBeginPath(args.vg);
            nvgRect(args.vg, RECT_ARGS(parent->box.zeroPos()));
            nvgRect(args.vg, RECT_ARGS(module->restrictionBox));
            nvgPathWinding(args.vg, NVG_HOLE);
            nvgFillColor(args.vg, nvgRGBAf(c.r, c.g, c.b, module->outsideAlpha));
            nvgFill(args.vg);

            // Outline the box
            nvgBeginPath(args.vg);
            nvgRect(args.vg, RECT_ARGS(module->restrictionBox));
            nvgStrokeWidth(args.vg, 4.0);
            nvgStrokeColor(args.vg, nvgRGBAf(c.r, c.g, c.b, std::max(0.f, module->outsideAlpha - 0.2f)));
            nvgStroke(args.vg);
        }
    }
}; // struct PanicRoomRestrictionWidget


struct PanicRoomWidget : ThemedModuleWidget<PanicRoomModule> {
    PanicRoomModule* module;
    PanicRoomRestrictionWidget* selectionWidget;
    std::set<ModuleWidget*> bypassedModules;

    PanicRoomWidget(PanicRoomModule* module) : ThemedModuleWidget<PanicRoomModule>(module, "PanicRoom") {
        setModule(module);
        this->module = dynamic_cast<PanicRoomModule*>(module);

        addChild(createWidget<StoermelderBlackScrew>(Vec(0, 0)));
        addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 1 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));


        if (module) {
            selectionWidget = new PanicRoomRestrictionWidget;
            selectionWidget->module = module;
            APP->scene->rack->addChild(selectionWidget);
        }
    }

    ~PanicRoomWidget() {
        if (module) {
            APP->scene->rack->removeChild(selectionWidget);
            delete selectionWidget;
        }
    }

    inline bool containsRestriction(const Vec& v) const {
        const Rect& r = module->restrictionBox;
        return (r.pos.x <= v.x) && (r.size.x == INFINITY || v.x <= r.pos.x + r.size.x)
            && (r.pos.y <= v.y) && (r.size.y == INFINITY || v.y <= r.pos.y + r.size.y);
    }

    inline bool isInsideRestriction(const Rect& m) {
        return containsRestriction(m.pos) && containsRestriction(m.pos + m.size);
    }

    void step() override {
        if (module && module->restrictionEnabled) {
            for (ModuleWidget* mw : APP->scene->rack->getModules()) {
                if (!isInsideRestriction(mw->box)) {
                    Module* m = mw->getModule();
                    if (!m->isBypassed()) {
                        APP->engine->bypassModule(m, true);
                        bypassedModules.insert(mw);
                    }
                }
                else {
                    if (bypassedModules.find(mw) != bypassedModules.end()) {
                        APP->engine->bypassModule(mw->getModule(), false);
                        bypassedModules.erase(mw);
                    }
                }
            }
        }
        ModuleWidget::step();
    }

    void appendContextMenu(ui::Menu* menu) override {
        ModuleWidget::appendContextMenu(menu);
        
        menu->addChild(new MenuSeparator());
        menu->addChild(createMenuItem("Learn", "", [=]() { selectionWidget->enableLearn(); }));
        menu->addChild(createMenuItem("Clear", "", [=]() { module->restrictionEnabled = false; }));

        menu->addChild(new MenuSeparator());
        menu->addChild(createSubmenuItem("Outside color", "", 
            [=](Menu* menu) {
                menu->addChild(construct<MenuColorLabel>(&MenuColorLabel::fillColor, &module->outsideColor));
                menu->addChild(new MenuSeparator);
                menu->addChild(construct<MenuColorPicker>(&MenuColorPicker::color, &module->outsideColor));
                menu->addChild(new MenuSeparator);
                menu->addChild(createMenuItem("Reset to default", "", [=]() {
                    module->outsideColor = nvgRGBAf(0.f, 0.f, 0.f, 1.f);
                }));
            }
        ));

        struct AlphaSlider : ui::Slider {
            struct AlphaQuantity : Quantity {
                PanicRoomModule* module;

                AlphaQuantity(PanicRoomModule* module) {
                    this->module = module;
                }
                void setValue(float value) override {
                    module->outsideAlpha = math::clamp(value, 0.f, 1.f);
                }
                float getValue() override {
                    return module->outsideAlpha;
                }
                float getDefaultValue() override {
                    return 0.5f;
                }
                float getDisplayValue() override {
                    return getValue() * 100;
                }
                void setDisplayValue(float displayValue) override {
                    setValue(displayValue / 100.f);
                }
                std::string getLabel() override {
                    return "Opacity";
                }
                std::string getUnit() override {
                    return "%";
                }
            };

            AlphaSlider(PanicRoomModule* module) {
                this->box.size.x = 200.0;
                quantity = new AlphaQuantity(module);
            }
            ~AlphaSlider() {
                delete quantity;
            }
        };

        menu->addChild(new AlphaSlider(module));
    }
    };

} // namespace PanicRoom
} // namespace StoermelderPackOne

Model* modelPanicRoom= createModel<StoermelderPackOne::PanicRoom::PanicRoomModule, StoermelderPackOne::PanicRoom::PanicRoomWidget>("PanicRoom");