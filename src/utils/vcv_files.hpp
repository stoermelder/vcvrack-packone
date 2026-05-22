#pragma once
#include "../plugin.hpp"
#include "StripIdFixModule.hpp"
#include <osdialog.h>


namespace StoermelderPackOne {
namespace vcv {

static const char SELECTION_FILTERS[] = "VCV Rack module selection (.vcvs):vcvs";

/**
 * Creates a module from JSON data, also returns the previous id of the module.
 * @param moduleJ JSON representation of the module
 * @param oldId Output parameter for the previous module id
 * @return ModuleWidget pointer if successful, NULL otherwise
 */
static ModuleWidget* moduleFromJson(json_t* moduleJ, int64_t& oldId) {
    // Get slugs
    json_t* pluginSlugJ = json_object_get(moduleJ, "plugin");
    if (!pluginSlugJ)
        return NULL;
    json_t* modelSlugJ = json_object_get(moduleJ, "model");
    if (!modelSlugJ)
        return NULL;
    std::string pluginSlug = json_string_value(pluginSlugJ);
    std::string modelSlug = json_string_value(modelSlugJ);

    json_t* idJ = json_object_get(moduleJ, "id");
    oldId = idJ ? json_integer_value(idJ) : -1;

    // Get Model
    plugin::Model* model = plugin::getModel(pluginSlug, modelSlug);
    if (!model) return NULL;

    // Create Module
    engine::Module* addedModule = model->createModule();
    APP->engine->addModule(addedModule);

    // Create ModuleWidget
    ModuleWidget* moduleWidget = model->createModuleWidget(addedModule);
    assert(moduleWidget);
    return moduleWidget;
}


enum class moduleToRackPos {
    LEFT,
    RIGHT,
    POS
};

/**
 * Adds a new module to the rack from a JSON representation.
 * @param moduleJ JSON representation of the module
 * @param modPos Position mode - LEFT places module left of box, RIGHT to the right, POS at box position
 * @param box The bounding box, updated with the new module's position and size
 * @param oldId Output parameter for the previous module id
 * @return ModuleWidget pointer if successful, NULL otherwise
 */
static ModuleWidget* moduleToRack(json_t* moduleJ, moduleToRackPos modPos, Rect& box, int64_t& oldId) {
    ModuleWidget* moduleWidget = moduleFromJson(moduleJ, oldId);
    if (moduleWidget) {
        switch (modPos) {
            case moduleToRackPos::LEFT:
                moduleWidget->box.pos = box.pos.minus(Vec(moduleWidget->box.size.x, 0));
                break;
            case moduleToRackPos::RIGHT:
                moduleWidget->box.pos = box.pos;
                break;
            case moduleToRackPos::POS:
                //box.pos = box.pos.mult(RACK_GRID_SIZE);
                moduleWidget->box.pos = box.pos; //.plus(RACK_OFFSET);
                break;
        }

        APP->scene->rack->addModule(moduleWidget);
        APP->scene->rack->setModulePosForce(moduleWidget, moduleWidget->box.pos);
        box.size = moduleWidget->box.size;
        box.pos = moduleWidget->box.pos;
        return moduleWidget;
    }
    else {
        json_t* pluginSlugJ = json_object_get(moduleJ, "plugin");
        std::string pluginSlug = json_string_value(pluginSlugJ);
        json_t* modelSlugJ = json_object_get(moduleJ, "model");
        std::string modelSlug = json_string_value(modelSlugJ);
        //warningLog += string::f("Could not find module \"%s\" of plugin \"%s\"\n", modelSlug.c_str(), pluginSlug.c_str());
        box = Rect(box.pos, Vec(0, 0));
        return NULL;
    }
}


/**
 * Checks for unavailable modules in the selection and prompts user to view them on VCV Library.
 * @param rootJ JSON representation of the vcvs file
 */
static void vcvsCheckUnavailable(json_t* rootJ) {
    std::set<std::string> pluginModuleSlugs;

    json_t* modulesJ = json_object_get(rootJ, "modules");
    if (!modulesJ) return;

    size_t moduleIndex;
    json_t* moduleJ;
    json_array_foreach(modulesJ, moduleIndex, moduleJ) {
        try {
            // Get model
            plugin::modelFromJson(moduleJ);
        }
        catch (Exception& e) {
            // Get plugin and module slugs
            json_t* pluginSlugJ = json_object_get(moduleJ, "plugin");
            if (!pluginSlugJ) continue;

            std::string pluginSlug = json_string_value(pluginSlugJ);

            json_t* modelSlugJ = json_object_get(moduleJ, "model");
            if (!modelSlugJ) continue;

            std::string modelSlug = json_string_value(modelSlugJ);
            pluginModuleSlugs.insert(pluginSlug + "/" + modelSlug);
        }
    }

    if (!pluginModuleSlugs.empty()) {
        std::string msg = "This selection/strip includes modules that are not installed. Show missing modules on the VCV Library?";
        if (osdialog_message(OSDIALOG_WARNING, OSDIALOG_YES_NO, msg.c_str())) {
            std::string url = "https://library.vcvrack.com/?modules=";
            url += string::join(pluginModuleSlugs, ",");
            system::openBrowser(url);
        }
    }
}


/**
 * Deserializes modules from JSON and adds them to the rack.
 * Updates the modules map with old-to-new module id mappings.
 * @param rootJ JSON representation of the vcvs file
 * @param modules Map to store old module id -> new ModuleWidget mappings
 * @return Vector of history actions for undo support
 */
static std::vector<history::Action*>* vcvsFromJson_modules(json_t* rootJ, std::map<int64_t, ModuleWidget*>& modules) {
    std::vector<history::Action*>* undoActions = new std::vector<history::Action*>;

    Vec mousePos = APP->scene->rack->getMousePos();
    json_t* modulesJ = json_object_get(rootJ, "modules");
    if (modulesJ) {
        json_t* moduleJ;
        size_t moduleIndex;

        double minX = std::numeric_limits<float>::infinity();
        double minY = std::numeric_limits<float>::infinity();
        json_array_foreach(modulesJ, moduleIndex, moduleJ) {
            // pos
            json_t* posJ = json_object_get(moduleJ, "pos");
            double x = 0.0, y = 0.0;
            json_unpack(posJ, "[F, F]", &x, &y);
            minX = std::min(minX, x);
            minY = std::min(minY, y);
        }

        json_array_foreach(modulesJ, moduleIndex, moduleJ) {
            int64_t oldId = -1;

            // pos
            Rect box;
            json_t* posJ = json_object_get(moduleJ, "pos");
            double x = 0.0, y = 0.0;
            json_unpack(posJ, "[F, F]", &x, &y);
            box.pos = math::Vec(x, y);
            box.pos = box.pos.minus(Vec(minX, minY)).mult(RACK_GRID_SIZE);
            box.pos = mousePos.plus(box.pos);

            ModuleWidget* mw = moduleToRack(moduleJ, moduleToRackPos::POS, box, oldId);
            // mw could be NULL, just move on
            modules[oldId] = mw;

            if (mw) {
                // ModuleAdd history action
                history::ModuleAdd* h = new history::ModuleAdd;
                h->name = "create module";
                h->setModule(mw);
                undoActions->push_back(h);
            }

            APP->scene->rack->select(mw);
        }
    }

    return undoActions;
}


/**
 * Fixes parameter mappings within a preset. This is a workaround because
 * Rack v1/v2 offers no API for reading the mapping module of a parameter. This replaces the
 * module id in the preset JSON with the new module id to preserve correct mapping.
 * Only handles specific modules known to use parameter mappings (Core/MIDI-Map, MindMeldModular/PatchMaster).
 * @param moduleJ JSON representation of the module
 * @param modules Maps old module ids to new ModuleWidgets
 */
static void vcvsFromJson_presets_fixMapping(json_t* moduleJ, std::map<int64_t, ModuleWidget*>& modules) {
    std::string pluginSlug = json_string_value(json_object_get(moduleJ, "plugin"));
    std::string modelSlug = json_string_value(json_object_get(moduleJ, "model"));

    static const std::set<std::tuple<std::string, std::string>> moduleSlugs = {
        std::make_tuple("Core", "MIDI-Map"),
        std::make_tuple("MindMeldModular", "PatchMaster")
    };

    // Only handle some specific modules known to use mapping of parameters
    if (moduleSlugs.find(std::make_tuple(pluginSlug, modelSlug)) == moduleSlugs.end())
        return;

    json_t* dataJ = json_object_get(moduleJ, "data");
    json_t* mapsJ = json_object_get(dataJ, "maps");
    if (mapsJ) {
        json_t* mapJ;
        size_t mapIndex;
        json_array_foreach(mapsJ, mapIndex, mapJ) {
            json_t* moduleIdJ = json_object_get(mapJ, "moduleId");
            if (!moduleIdJ)
                continue;
            int64_t oldId = json_integer_value(moduleIdJ);
            if (oldId >= 0) {
                int64_t newId = -1;
                ModuleWidget* mw = modules[oldId];
                if (mw != NULL) {
                    newId = mw->module->id;
                }
                json_object_set_new(mapJ, "moduleId", json_integer(newId));
            }
        }
    }
}


/**
 * Loads module presets from JSON and applies them to the loaded modules.
 * @param rootJ JSON representation of the vcvs file
 * @param modules Map of old module id -> new ModuleWidget
 * @return Vector of history actions for undo support
 */
static std::vector<history::Action*>* vcvsFromJson_presets(json_t* rootJ, std::map<int64_t, ModuleWidget*>& modules) {
    std::vector<history::Action*>* undoActions = new std::vector<history::Action*>;

    json_t* modulesJ = json_object_get(rootJ, "modules");
    json_t* moduleJ;
    size_t moduleIndex;
    json_array_foreach(modulesJ, moduleIndex, moduleJ) {
        vcvsFromJson_presets_fixMapping(moduleJ, modules);
        int64_t oldId = json_integer_value(json_object_get(moduleJ, "id"));
        ModuleWidget* mw = modules[oldId];
        if (mw != NULL) {
            // history::ModuleChange
            history::ModuleChange* h = new history::ModuleChange;
            h->name = "load module preset";
            h->moduleId = mw->module->id;
            h->oldModuleJ = mw->toJson();

            StripIdFixModule* m = dynamic_cast<StripIdFixModule*>(mw->module);
            if (m) m->idFixDataFromJson(modules);

            mw->fromJson(moduleJ);

            h->newModuleJ = mw->toJson();
            undoActions->push_back(h);
        }
    }

    return undoActions;
}


/**
 * Adds cables from JSON to the rack. Skips cables when either the output or input module
 * could not be loaded.
 * @param rootJ JSON representation of the STRIP/vcvs file
 * @param modules Map of old module id -> new ModuleWidget
 * @return Vector of history actions for undo support
 */
static std::vector<history::Action*>* vcvsFromJson_cables(json_t* rootJ, std::map<int64_t, ModuleWidget*>& modules) {
    std::vector<history::Action*>* undoActions = new std::vector<history::Action*>;

    json_t* cablesJ = json_object_get(rootJ, "cables");
    if (cablesJ) {
        json_t* cableJ;
        size_t cableIndex;
        json_array_foreach(cablesJ, cableIndex, cableJ) {
            int64_t outputModuleId = json_integer_value(json_object_get(cableJ, "outputModuleId"));
            int outputId = json_integer_value(json_object_get(cableJ, "outputId"));
            int64_t inputModuleId = json_integer_value(json_object_get(cableJ, "inputModuleId"));
            int inputId = json_integer_value(json_object_get(cableJ, "inputId"));
            const char* colorStr = json_string_value(json_object_get(cableJ, "color"));

            ModuleWidget* outputModule = modules[outputModuleId];
            ModuleWidget* inputModule = modules[inputModuleId];
            // In case one of the modules could not be loaded
            if (!outputModule || !inputModule) continue;

            engine::Cable* c = new engine::Cable;
            c->outputModule = outputModule->module;
            c->outputId = outputId;
            //cw->setOutput(port);
            c->inputModule = inputModule->module;
            c->inputId = inputId;
            //cw->setInput(port);
            APP->engine->addCable(c);

            CableWidget* cw = new CableWidget;
            cw->setCable(c);
            if (colorStr) {
                cw->color = color::fromHexString(colorStr);
            }
            APP->scene->rack->addCable(cw);

            // history::CableAdd
            history::CableAdd* h = new history::CableAdd;
            h->setCable(cw);
            undoActions->push_back(h);
        }
    }

    return undoActions;
}


/**
 * Main entry point for loading a vcvs selection from JSON.
 * Orchestrates module creation, preset loading, and cable connections.
 * Creates a single complex history action for undo support.
 * @param rootJ JSON representation of the vcvs file
 * @return Warning log string for any issues encountered
 */
static const std::string vcvsFromJson(json_t* rootJ, std::string undoActionName = "") {
    std::string warningLog = "";

    // Maps old moduleId to the newly created modules (with new id)
    std::map<int64_t, ModuleWidget*> modules;
    // Add modules
    std::vector<history::Action*>* h2 = vcvsFromJson_modules(rootJ, modules);
    // Load presets for modules, also fixes parameter mappings
    std::vector<history::Action*>* h3 = vcvsFromJson_presets(rootJ, modules);

    // Add cables
    std::vector<history::Action*>* h4 = vcvsFromJson_cables(rootJ, modules);

    // Does nothing, but fixes https://github.com/VCVRack/Rack/issues/1444 for Rack <= 1.1.1
    //APP->scene->rack->requestModulePos(this, this->box.pos);

    if (!warningLog.empty()) {
        osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK, warningLog.c_str());
    }

    history::ComplexAction* complexAction = new history::ComplexAction;
    complexAction->name = undoActionName.empty() ? "Load selection" : undoActionName;
    for (history::Action* h : *h2) complexAction->push(h);
    delete h2;
    for (history::Action* h : *h3) complexAction->push(h);
    delete h3;
    for (history::Action* h : *h4) complexAction->push(h);
    delete h4;
    APP->history->push(complexAction);

    return warningLog;
}


/**
 * Loads a vcvs selection file from the given path.
 * Validates JSON, checks for unavailable modules, and loads the selection.
 * @param path Full path to the .vcvs file
 */
static void vcvsLoadFile(std::string path, std::string undoActionName = "") {
    FILE* file = std::fopen(path.c_str(), "r");
    if (!file) return;
    DEFER({std::fclose(file);});
    INFO("Loading selection %s", path.c_str());

    json_error_t error;
    json_t* rootJ = json_loadf(file, 0, &error);
    if (!rootJ) {
        throw Exception("File is not a valid selection file. JSON parsing error at %s %d:%d %s", error.source, error.line, error.column, error.text);
    }
    DEFER({json_decref(rootJ);});

    vcvsCheckUnavailable(rootJ);
    vcvsFromJson(rootJ, undoActionName);
}


/**
 * Loads a vcvs selection from the system clipboard.
 * Deselects all modules, parses clipboard JSON, and loads the selection.
 * Shows an error dialog if clipboard access or JSON parsing fails.
 */
static void vcvsPasteClipboard(std::string undoActionName = "") {
    APP->scene->rack->deselectAll();

    const char* moduleJson = glfwGetClipboardString(APP->window->win);
    if (!moduleJson) {
        osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK, "Could not get text from clipboard.");
        return;
    }

    json_error_t error;
    json_t* rootJ = json_loads(moduleJson, 0, &error);
    if (!rootJ) {
        std::string message = string::f("JSON parsing error at %s %d:%d %s", error.source, error.line, error.column, error.text);
        osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK, message.c_str());
        return;
    }
    DEFER({
        json_decref(rootJ);
    });

    vcvsFromJson(rootJ, undoActionName);
}


/**
 * Shows an open file dialog and loads a vcvs selection from the selected file.
 * @param load If true, actually loads the file; if false, just returns the selected path
 * @return The selected file path, or empty string if cancelled
 */
static std::string vcvsLoadFileDialog(bool load, std::string undoActionName = "") {
    osdialog_filters* filters = osdialog_filters_parse(SELECTION_FILTERS);
    DEFER({osdialog_filters_free(filters);});

    char* pathC = osdialog_file(OSDIALOG_OPEN, pluginSettings.stripDirVcvs.c_str(), NULL, filters);
    if (!pathC) {
        // No path selected
        return "";
    }
    DEFER({
        pluginSettings.stripDirVcvs = system::getDirectory(std::string(pathC));
        pluginSettings.saveToJson();
        std::free(pathC);
    });

    try {
        if (load) vcvsLoadFile(pathC, undoActionName);
    }
    catch (Exception& e) {
        osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK, e.what());
    }

    return std::string(pathC);
}



/**
 * Gets the width of a module from its model by looking up the plugin/model slugs.
 * @param moduleJ JSON representation of the module
 * @return Module width in pixels, or 0 if model not found
 */
static float getModuleWidth(json_t* moduleJ) {
    json_t* pluginSlugJ = json_object_get(moduleJ, "plugin");
    if (!pluginSlugJ) return 0.f;
    json_t* modelSlugJ = json_object_get(moduleJ, "model");
    if (!modelSlugJ) return 0.f;
    
    std::string pluginSlug = json_string_value(pluginSlugJ);
    std::string modelSlug = json_string_value(modelSlugJ);
    
    plugin::Model* model = plugin::getModel(pluginSlug, modelSlug);
    if (!model) return 0.f;
    
    // Get the module widget to determine width
    ModuleWidget* mw = model->createModuleWidget(nullptr);
    if (!mw) return 0.f;
    float width = mw->box.size.x;
    delete mw; // Clean up temporary widget
    return width;
}


/**
 * Converts a vcvss (STRIP) JSON to vcvs format by combining rightModules and leftModules
 * into a single modules array with proper positions calculated from model widths.
 * 
 * The vcvss format does not store absolute positions - modules are positioned relative
 * to each other based on their widths. This function calculates positions using the
 * model dimensions.
 * 
 * @param vcvssJ JSON representation of the vcvss file
 * @return JSON object with vcvs format (modules array with positions, cables array),
 *         or nullptr if conversion fails. Caller must call json_decref on result.
 */
static json_t* convertVcvssToVcvs(json_t* vcvssJ) {
    if (!vcvssJ || !json_is_object(vcvssJ)) return nullptr;
    
    json_t* vcvsJ = json_object();
    if (!vcvsJ) return nullptr;
    
    json_t* modulesJ = json_array();
    if (!modulesJ) {
        json_decref(vcvsJ);
        return nullptr;
    }
    
    // Process left modules first - they appear on the left in the strip
    // Note: leftModules are stored right-to-left (first element = rightmost module)
    // So we need to reverse the order for proper left-to-right positioning
    float currentX = 0.f;
    json_t* leftModulesJ = json_object_get(vcvssJ, "leftModules");
    if (leftModulesJ && json_is_array(leftModulesJ)) {
        size_t leftCount = json_array_size(leftModulesJ);
        
        // First pass: collect widths and calculate total left width
        std::vector<float> leftWidths;
        leftWidths.reserve(leftCount);
        for (size_t i = 0; i < leftCount; i++) {
            json_t* moduleJ = json_array_get(leftModulesJ, i);
            leftWidths.push_back(getModuleWidth(moduleJ) / RACK_GRID_SIZE.x);
        }
        
        // Start left modules at position 0 (leftmost will be at x=0)
        currentX = 0.f;
        
        // Add left modules in reverse order (last element = leftmost = first in array)
        for (size_t i = leftCount; i > 0; i--) {
            size_t idx = i - 1;
            json_t* moduleJ = json_array_get(leftModulesJ, idx);
            json_t* clonedModule = json_deep_copy(moduleJ);
            if (!clonedModule) continue;
            
            // Set position in RACK_GRID_SIZE units
            json_object_del(clonedModule, "pos");
            json_t* posJ = json_array();
            json_array_append_new(posJ, json_real(currentX));
            json_array_append_new(posJ, json_real(0.0));
            json_object_set_new(clonedModule, "pos", posJ);
            
            json_array_append_new(modulesJ, clonedModule);
            currentX += leftWidths[idx];
        }
    }
    
    // Process right modules - they appear on the right in the strip
    // Continue after left modules (if any)
    json_t* rightModulesJ = json_object_get(vcvssJ, "rightModules");
    if (rightModulesJ && json_is_array(rightModulesJ)) {
        json_t* moduleJ;
        size_t idx;
        json_array_foreach(rightModulesJ, idx, moduleJ) {
            json_t* clonedModule = json_deep_copy(moduleJ);
            if (!clonedModule) continue;
            
            // Convert pixel width to RACK_GRID_SIZE units
            float width = getModuleWidth(moduleJ) / RACK_GRID_SIZE.x;
            
            // Set position in RACK_GRID_SIZE units
            json_object_del(clonedModule, "pos");
            json_t* posJ = json_array();
            json_array_append_new(posJ, json_real(currentX));
            json_array_append_new(posJ, json_real(0.0));
            json_object_set_new(clonedModule, "pos", posJ);
            
            json_array_append_new(modulesJ, clonedModule);
            currentX += width;
        }
    }
    
    json_object_set_new(vcvsJ, "modules", modulesJ);
    
    // Copy cables (adjust module IDs if needed - for now, keep as-is)
    json_t* cablesJ = json_object_get(vcvssJ, "cables");
    if (cablesJ) {
        json_object_set(vcvsJ, "cables", cablesJ);
        json_incref(cablesJ);
    }
    else {
        json_object_set_new(vcvsJ, "cables", json_array());
    }
    
    return vcvsJ;
}

} // namespace vcv
} // namespace StoermelderPackOne
