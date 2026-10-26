#include "../../plugin.hpp"
#include "../../vcv/fs.hpp"
#include "Mb_ratings.hpp"

namespace StoermelderPackOne {
namespace Mb {

std::map<Model*, int> modelRatings;

// Entries of the ratings file whose model couldn't be resolved at load time (plugin
// uninstalled or disabled). Same shape as the file's "ratings" object, kept so that
// saving doesn't silently drop them.
static json_t* unresolvedRatingsJ = NULL;

static std::string mbRatingsFilePath() {
	return vcv::fs::getUserDirectory("Stoermelder-P1/mb-ratings.json");
}

int modelRatingGet(Model* model) {
	auto it = modelRatings.find(model);
	return (it != modelRatings.end()) ? it->second : 0;
}

void modelRatingSet(Model* model, int rating) {
	rating = math::clamp(rating, 0, RATING_MAX);
	if (modelRatingGet(model) == rating) return;
	if (rating == 0)
		modelRatings.erase(model);
	else
		modelRatings[model] = rating;
	modelRatingsToJson();
}

void modelRatingsFromJson() {
	modelRatings.clear();
	if (unresolvedRatingsJ) {
		json_decref(unresolvedRatingsJ);
		unresolvedRatingsJ = NULL;
	}

	std::string data;
	if (!vcv::fs::read(mbRatingsFilePath(), data)) return;
	json_error_t error;
	json_t* j = json_loads(data.c_str(), 0, &error);
	if (!j) return;
	DEFER({ json_decref(j); });

	json_t* ratingsJ = json_object_get(j, "ratings");
	if (!json_is_object(ratingsJ)) return;

	unresolvedRatingsJ = json_object();
	const char* pluginSlug;
	json_t* pluginJ;
	json_object_foreach(ratingsJ, pluginSlug, pluginJ) {
		if (!json_is_object(pluginJ)) continue;
		const char* modelSlug;
		json_t* ratingJ;
		json_object_foreach(pluginJ, modelSlug, ratingJ) {
			if (!json_is_integer(ratingJ)) continue;
			json_int_t value = json_integer_value(ratingJ);
			int rating = (value > RATING_MAX) ? RATING_MAX : (int)std::max(value, (json_int_t)0);
			if (rating <= 0) continue;
			Model* model = plugin::getModel(pluginSlug, modelSlug);
			if (model) {
				modelRatings[model] = rating;
			}
			else {
				json_t* unresolvedPluginJ = json_object_get(unresolvedRatingsJ, pluginSlug);
				if (!unresolvedPluginJ) {
					unresolvedPluginJ = json_object();
					json_object_set_new(unresolvedRatingsJ, pluginSlug, unresolvedPluginJ);
				}
				json_object_set_new(unresolvedPluginJ, modelSlug, json_integer(rating));
			}
		}
	}
}

void modelRatingsToJson() {
	json_t* ratingsJ = json_object();
	for (auto& pair : modelRatings) {
		Model* model = pair.first;
		json_t* pluginJ = json_object_get(ratingsJ, model->plugin->slug.c_str());
		if (!pluginJ) {
			pluginJ = json_object();
			json_object_set_new(ratingsJ, model->plugin->slug.c_str(), pluginJ);
		}
		json_object_set_new(pluginJ, model->slug.c_str(), json_integer(pair.second));
	}

	// Merge back the ratings of models that weren't available at load time
	if (unresolvedRatingsJ) {
		const char* pluginSlug;
		json_t* unresolvedPluginJ;
		json_object_foreach(unresolvedRatingsJ, pluginSlug, unresolvedPluginJ) {
			json_t* pluginJ = json_object_get(ratingsJ, pluginSlug);
			if (!pluginJ) {
				pluginJ = json_object();
				json_object_set_new(ratingsJ, pluginSlug, pluginJ);
			}
			const char* modelSlug;
			json_t* ratingJ;
			json_object_foreach(unresolvedPluginJ, modelSlug, ratingJ) {
				if (json_object_get(pluginJ, modelSlug)) continue;
				json_object_set(pluginJ, modelSlug, ratingJ);
			}
		}
	}

	json_t* j = json_object();
	json_object_set_new(j, "version", json_integer(1));
	json_object_set_new(j, "ratings", ratingsJ);

	vcv::fs::createDirectory(vcv::fs::getUserDirectory("Stoermelder-P1"));
	char* dump = json_dumps(j, JSON_INDENT(2) | JSON_SORT_KEYS);
	if (dump) {
		vcv::fs::write(mbRatingsFilePath(), dump);
		free(dump);
	}
	json_decref(j);
}


std::string ratingStarsText(int n) {
	std::string s;
	for (int i = 0; i < n; i++)
		s += "\xE2\x98\x85"; // U+2605
	return s;
}

std::vector<math::Rect> ratingStarRects(math::Vec size, int rating) {
	std::vector<math::Rect> rects;
	rating = math::clamp(rating, 0, RATING_MAX);
	if (rating == 0) return rects;

	// Start from the height-based size, then shrink it so the column also fits a
	// narrow (1 HP) box, where the margin is a quarter of the star on each side.
	float starSize = std::min(size.y * 0.045f, size.x / 1.5f);
	if (starSize < 4.f) return rects;
	float margin = starSize * 0.25f;
	float gap = starSize * 0.15f;
	for (int i = 0; i < rating; i++) {
		rects.push_back(math::Rect(size.x - margin - starSize, size.y - margin - (i + 1) * starSize - i * gap, starSize, starSize));
	}
	return rects;
}

void drawRatingStars(NVGcontext* vg, math::Vec size, int rating) {
	for (const math::Rect& r : ratingStarRects(size, rating)) {
		math::Vec c = r.getCenter();
		float outer = r.size.x / 2.f;
		float inner = outer * 0.4f;
		nvgBeginPath(vg);
		for (int k = 0; k < 10; k++) {
			float radius = (k % 2 == 0) ? outer : inner;
			float a = -M_PI / 2.f + k * M_PI / 5.f;
			float x = c.x + std::cos(a) * radius;
			float y = c.y + std::sin(a) * radius;
			if (k == 0) nvgMoveTo(vg, x, y);
			else nvgLineTo(vg, x, y);
		}
		nvgClosePath(vg);
		nvgFillColor(vg, componentlibrary::SCHEME_YELLOW);
		nvgFill(vg);
		nvgStrokeWidth(vg, r.size.x * 0.08f);
		nvgStrokeColor(vg, nvgRGBAf(0.f, 0.f, 0.f, 0.6f));
		nvgStroke(vg);
	}
}

} // namespace Mb
} // namespace StoermelderPackOne
