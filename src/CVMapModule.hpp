#pragma once
#include "plugin.hpp"
#include "MapModule.hpp"

template< int MAX_CHANNELS >
struct CVMapModule : MapModule<MAX_CHANNELS> {
	bool bipolarInput = false;

	/** Track last values */
	float lastValue[MAX_CHANNELS];
	/** [Saved to JSON] Allow manual changes of target parameters */
	bool lockParameterChanges = true;

	dsp::ClockDivider lightDivider;

	CVMapModule() {
		for (int id = 0; id < MAX_CHANNELS; id++) {
			MapModule<MAX_CHANNELS>::paramHandles[id].color = nvgRGB(0xff, 0x40, 0xff);
		}
		lightDivider.setDivision(1024);
	}

	void process(const Module::ProcessArgs &args) override {
		MapModule<MAX_CHANNELS>::process(args);
	}

	json_t *dataToJson() override {
		json_t *rootJ = MapModule<MAX_CHANNELS>::dataToJson();
		json_object_set_new(rootJ, "lockParameterChanges", json_boolean(lockParameterChanges));
		json_object_set_new(rootJ, "bipolarInput", json_boolean(bipolarInput));

		return rootJ;
	}

	void dataFromJson(json_t *rootJ) override {
		MapModule<MAX_CHANNELS>::dataFromJson(rootJ);

		json_t *lockParameterChangesJ = json_object_get(rootJ, "lockParameterChanges");
		lockParameterChanges = json_boolean_value(lockParameterChangesJ);

		json_t *bipolarInputJ = json_object_get(rootJ, "bipolarInput");
		bipolarInput = json_boolean_value(bipolarInputJ);
	}
};