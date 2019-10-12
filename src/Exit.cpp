#include "plugin.hpp"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <patch.hpp>
#include <osdialog.h>


namespace Exit {

static const char PATCH_FILTERS[] = "VCV Rack patch (.vcv):vcv";

struct ExitModule : Module {
	enum ParamIds {
		NUM_PARAMS
	};
	enum InputIds {
		TRIG_INPUT,
		TRIGS_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	std::string path;

	std::thread* worker;
    std::mutex workerMutex;
	std::condition_variable workerCondVar;
	int workToDo = 0;

	dsp::SchmittTrigger trigTrigger;
	dsp::SchmittTrigger trigsTrigger;

	ExitModule() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		worker = new std::thread(&ExitModule::workerProcess, this);
	}

	void process(const ProcessArgs &args) override {
        if (inputs[TRIG_INPUT].isConnected() && trigTrigger.process(inputs[TRIG_INPUT].getVoltage())) {
            workToDo = 1;
            workerCondVar.notify_one();
        }
        if (inputs[TRIGS_INPUT].isConnected() && trigTrigger.process(inputs[TRIGS_INPUT].getVoltage())) {
            workToDo = 2;
            workerCondVar.notify_one();
        }
	}

	void workerProcess() {
        {
            std::unique_lock<std::mutex> lock(workerMutex);
            workerCondVar.wait(lock);
        }
        if (!path.empty()) {
            std::string path = this->path;
            if (workToDo == 2)
                APP->patch->save(APP->patch->path);
            APP->patch->load(path);
            APP->patch->path = path;
            APP->history->setSaved();
        }
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();

        json_t* pathJ = json_string(path.c_str());
        json_object_set_new(rootJ, "path", pathJ);
        return rootJ;
	}

	void dataFromJson(json_t *rootJ) override {
        json_t* pathJ = json_object_get(rootJ, "path");
        if (pathJ)
            path = json_string_value(pathJ);
        }
};


struct ExitWidget : ModuleWidget {
    ExitModule* module;

	ExitWidget(ExitModule* module) {
		setModule(module);
        this->module = module;
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/Exit.svg")));

		addChild(createWidget<MyBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<MyBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 280.2f), module, ExitModule::TRIG_INPUT));
		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 323.7f), module, ExitModule::TRIGS_INPUT));
	}

	void selectFileDialog() {
        std::string dir;
        if (module->path.empty()) {
            dir = asset::user("patches");
            system::createDirectory(dir);
        }
        else {
            dir = string::directory(module->path);
        }

        osdialog_filters* filters = osdialog_filters_parse(PATCH_FILTERS);
        DEFER({
            osdialog_filters_free(filters);
        });

        char* pathC = osdialog_file(OSDIALOG_OPEN, dir.c_str(), NULL, filters);
        if (!pathC) {
            // Fail silently
            return;
        }
        DEFER({
            std::free(pathC);
        });

        module->path = pathC;
	}

	void appendContextMenu(Menu* menu) override {
		menu->addChild(new MenuSeparator());

		struct SelectFileItem : MenuItem {
			ExitWidget* widget;
			void onAction(const event::Action &e) override {
                widget->selectFileDialog();
			}
		};

		menu->addChild(construct<SelectFileItem>(&MenuItem::text, "Select patch", &SelectFileItem::widget, this));

		if (module->path != "") {
			ui::MenuLabel* textLabel = new ui::MenuLabel;
			textLabel->text = "Currently selected...";
			menu->addChild(textLabel);

			ui::MenuLabel* modelLabel = new ui::MenuLabel;
			modelLabel->text = module->path;
			menu->addChild(modelLabel);
		}
	}
};

} // namespace Exit

Model* modelExit = createModel<Exit::ExitModule, Exit::ExitWidget>("Exit");