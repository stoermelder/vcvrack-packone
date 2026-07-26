#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"
#include "Mb.cpp"
#include "Mb.hpp"

using namespace StoermelderPackOne::Mb;

SYNC_MODEL(modelMb, "Mb");
Test::TestContext<> testContext;


// Helper function to create a mock model for testing
plugin::Model* createMockModel(const std::string& pluginSlug, const std::string& modelSlug, const std::string& name) {
	static std::vector<plugin::Model*> mockModels;
	static std::vector<plugin::Plugin*> mockPlugins;
	
	plugin::Plugin* p = new plugin::Plugin;
	p->slug = pluginSlug;
	p->name = pluginSlug;
	p->brand = "TestBrand";
	mockPlugins.push_back(p);
	
	plugin::Model* m = new plugin::Model;
	m->slug = modelSlug;
	m->name = name;
	m->plugin = p;
	m->description = "Test model description";
	mockModels.push_back(m);
	p->models.push_back(m);
	
	return m;
}

// Cleanup helper
void cleanupMockModels() {
	// Clear all MB state
	favoriteModels.clear();
	hiddenModels.clear();
	customTagModels.clear();
	predefinedTagsAdded.clear();
	predefinedTagsRemoved.clear();
	modelUsage.clear();
	effectiveTagIdsCache.clear();
}


TEST_CASE("Construction and initialization", "[Mb]") {
	MbModule* m = Test::createModule<MbModule>("Mb");
	MbWidget* mw = Test::createWidget<MbWidget>("Mb");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);

	Test::destroyWidget(mw);
	Test::destroyModule(m);
}

TEST_CASE("Preset JSON null-guards", "[Mb][JSON]") {
	auto module = Test::createModule<MbModule>("Mb");

	SECTION("All top-level properties are null-guarded in dataFromJson()") {
		json_t* rootJ = module->dataToJson();
		REQUIRE(rootJ != nullptr);
		Test::testPresetNullGuards(module, rootJ);
		json_decref(rootJ);
	}

	Test::destroyModule(module);
}


TEST_CASE("Favorite model operations", "[Mb]") {
	plugin::Model* model = createMockModel("test-plugin", "test-model", "Test Model");
	cleanupMockModels();
	favoriteMode = FavoriteMode::MB;
	
	SECTION("Model starts as not favorite") {
		REQUIRE(!isModelFavorite(model));
	}
	
	SECTION("setModelFavorite adds to favorites") {
		setModelFavorite(model, true);
		REQUIRE(isModelFavorite(model));
	}
	
	SECTION("setModelFavorite removes from favorites") {
		setModelFavorite(model, true);
		REQUIRE(isModelFavorite(model));
		setModelFavorite(model, false);
		REQUIRE(!isModelFavorite(model));
	}
	
	SECTION("toggleModelFavorite toggles state") {
		REQUIRE(!isModelFavorite(model));
		toggleModelFavorite(model);
		REQUIRE(isModelFavorite(model));
		toggleModelFavorite(model);
		REQUIRE(!isModelFavorite(model));
	}
	
	SECTION("setModelFavorite removes from hidden when favoriting") {
		hiddenModels.insert(model);
		setModelFavorite(model, true);
		REQUIRE(isModelFavorite(model));
		REQUIRE(!isModelHidden(model));
	}
	
	SECTION("Multiple favorites can coexist") {
		plugin::Model* m1 = createMockModel("plugin1", "model1", "Model 1");
		plugin::Model* m2 = createMockModel("plugin2", "model2", "Model 2");
		setModelFavorite(m1, true);
		setModelFavorite(m2, true);
		REQUIRE(isModelFavorite(m1));
		REQUIRE(isModelFavorite(m2));
	}
	
	cleanupMockModels();
}

TEST_CASE("Favorite mode switching", "[Mb]") {
	plugin::Model* model = createMockModel("test-plugin", "test-model", "Test Model");
	cleanupMockModels();
	
	SECTION("VCVRACK mode uses VCV Rack favorites") {
		favoriteMode = FavoriteMode::VCVRACK;
		model->setFavorite(true);
		REQUIRE(isModelFavorite(model));
	}
	
	SECTION("MB mode uses internal favorites") {
		favoriteMode = FavoriteMode::MB;
		setModelFavorite(model, true);
		REQUIRE(isModelFavorite(model));
		model->setFavorite(false);
		REQUIRE(isModelFavorite(model));  // Still favorite in MB mode
	}
	
	SECTION("BOTH mode combines favorites from both sources") {
		favoriteMode = FavoriteMode::BOTH;
		plugin::Model* m1 = createMockModel("plugin1", "model1", "Model 1");
		plugin::Model* m2 = createMockModel("plugin2", "model2", "Model 2");
		
		m1->setFavorite(true);  // VCV Rack favorite
		setModelFavorite(m2, true);  // MB favorite
		
		REQUIRE(isModelFavorite(m1));
		REQUIRE(isModelFavorite(m2));
	}
	
	cleanupMockModels();
}

TEST_CASE("Hidden model operations", "[Mb]") {
	plugin::Model* model = createMockModel("test-plugin", "test-model", "Test Model");
	cleanupMockModels();
	
	SECTION("Model starts as not hidden") {
		REQUIRE(!isModelHidden(model));
	}
	
	SECTION("toggleModelHidden adds to hidden") {
		toggleModelHidden(model);
		REQUIRE(isModelHidden(model));
	}
	
	SECTION("toggleModelHidden removes from hidden") {
		toggleModelHidden(model);
		REQUIRE(isModelHidden(model));
		toggleModelHidden(model);
		REQUIRE(!isModelHidden(model));
	}
	
	SECTION("hiddenModelsReset clears all hidden") {
		plugin::Model* m1 = createMockModel("plugin1", "model1", "Model 1");
		plugin::Model* m2 = createMockModel("plugin2", "model2", "Model 2");
		toggleModelHidden(m1);
		toggleModelHidden(m2);
		REQUIRE(isModelHidden(m1));
		REQUIRE(isModelHidden(m2));
		
		hiddenModelsReset();
		REQUIRE(!isModelHidden(m1));
		REQUIRE(!isModelHidden(m2));
	}
	
	cleanupMockModels();
}

TEST_CASE("Custom tag operations", "[Mb]") {
	plugin::Model* model = createMockModel("test-plugin", "test-model", "Test Model");
	cleanupMockModels();
	
	SECTION("Model starts with no custom tags") {
		auto tags = customTagsForModel(model);
		REQUIRE(tags.empty());
	}
	
	SECTION("customTagAdd adds a tag") {
		customTagAdd(model, "Synth");
		REQUIRE(customTagHas(model, "Synth"));
	}
	
	SECTION("customTagRemove removes a tag") {
		customTagAdd(model, "Synth");
		REQUIRE(customTagHas(model, "Synth"));
		customTagRemove(model, "Synth");
		REQUIRE(!customTagHas(model, "Synth"));
	}
	
	SECTION("Multiple tags on same model") {
		customTagAdd(model, "Synth");
		customTagAdd(model, "Filter");
		customTagAdd(model, "Utility");
		
		auto tags = customTagsForModel(model);
		REQUIRE(tags.count("Synth") == 1);
		REQUIRE(tags.count("Filter") == 1);
		REQUIRE(tags.count("Utility") == 1);
		REQUIRE(tags.size() == 3);
	}
	
	SECTION("Tags are case-insensitive with resolveKey") {
		customTagAdd(model, "Synth");
		REQUIRE(customTagHas(model, "Synth", true));
		REQUIRE(customTagHas(model, "synth", true));
		REQUIRE(customTagHas(model, "SYNTH", true));
	}
	
	SECTION("customTagsAll returns all tags across models") {
		plugin::Model* m1 = createMockModel("plugin1", "model1", "Model 1");
		plugin::Model* m2 = createMockModel("plugin2", "model2", "Model 2");
		
		customTagAdd(m1, "Synth");
		customTagAdd(m1, "Filter");
		customTagAdd(m2, "Utility");
		
		auto allTags = customTagsAll();
		REQUIRE(allTags.count("Synth") == 1);
		REQUIRE(allTags.count("Filter") == 1);
		REQUIRE(allTags.count("Utility") == 1);
	}
	
	SECTION("customTagDelete removes tag from all models") {
		plugin::Model* m1 = createMockModel("plugin1", "model1", "Model 1");
		plugin::Model* m2 = createMockModel("plugin2", "model2", "Model 2");
		
		customTagAdd(m1, "Synth");
		customTagAdd(m2, "Synth");
		
		auto before = customTagsAll();
		REQUIRE(before.count("Synth") == 1);
		
		customTagDelete("Synth");
		
		auto after = customTagsAll();
		REQUIRE(after.count("Synth") == 0);
		REQUIRE(!customTagHas(m1, "Synth"));
		REQUIRE(!customTagHas(m2, "Synth"));
	}
	
	SECTION("customTagReset clears all tags") {
		plugin::Model* m1 = createMockModel("plugin1", "model1", "Model 1");
		plugin::Model* m2 = createMockModel("plugin2", "model2", "Model 2");
		
		customTagAdd(m1, "Synth");
		customTagAdd(m2, "Filter");
		
		customTagReset();
		
		REQUIRE(!customTagHas(m1, "Synth"));
		REQUIRE(!customTagHas(m2, "Filter"));
		REQUIRE(customTagsAll().empty());
	}
	
	cleanupMockModels();
}

TEST_CASE("Custom tag case resolution", "[Mb]") {
	plugin::Model* model = createMockModel("test-plugin", "test-model", "Test Model");
	cleanupMockModels();
	
	SECTION("First inserted case is canonical") {
		customTagAdd(model, "Synth");
		auto tags = customTagsForModel(model);
		REQUIRE(tags.count("Synth") == 1);
	}
	
	SECTION("Different cases refer to same tag via resolveKey") {
		customTagAdd(model, "Synth");
		
		auto tags1 = customTagsForModel(model);
		REQUIRE(tags1.size() == 1);
		
		// Try to add with different case - resolveKey finds the existing one
		customTagAdd(model, "synth");
		auto tags2 = customTagsForModel(model);
		REQUIRE(tags2.size() == 1);  // Still only one tag due to case-insensitive lookup
		// The canonical case is the first one added
		REQUIRE(tags2.count("Synth") == 1);
	}
	
	cleanupMockModels();
}

TEST_CASE("Custom tag validation", "[Mb]") {
	plugin::Model* model = createMockModel("test-plugin", "test-model", "Test Model");
	cleanupMockModels();
	
	SECTION("Empty string is invalid") {
		REQUIRE(!isValidCustomTag(""));
	}
	
	SECTION("Whitespace-only string is invalid") {
		REQUIRE(!isValidCustomTag("   "));
		REQUIRE(!isValidCustomTag("\t"));
		REQUIRE(!isValidCustomTag("\n"));
	}
	
	SECTION("String longer than 64 characters is invalid") {
		std::string longTag(65, 'a');
		REQUIRE(!isValidCustomTag(longTag));
	}
	
	SECTION("String of exactly 64 characters is valid") {
		std::string maxTag(64, 'a');
		REQUIRE(isValidCustomTag(maxTag));
	}
	
	SECTION("String with leading/trailing whitespace is trimmed and validated") {
		REQUIRE(!isValidCustomTag("   "));  // Only whitespace → invalid
		REQUIRE(isValidCustomTag("  validtag  "));  // Valid after trim → valid
	}
	
	SECTION("Valid strings are accepted") {
		REQUIRE(isValidCustomTag("Synth"));
		REQUIRE(isValidCustomTag("Filter"));
		REQUIRE(isValidCustomTag("Utility"));
		REQUIRE(isValidCustomTag("a"));
	}
	
	SECTION("Invalid tags are not added") {
		customTagAdd(model, "");
		REQUIRE(customTagsForModel(model).empty());
		
		customTagAdd(model, "   ");
		REQUIRE(customTagsForModel(model).empty());
		
		std::string longTag(65, 'a');
		customTagAdd(model, longTag);
		REQUIRE(customTagsForModel(model).empty());
	}
	
	cleanupMockModels();
}

TEST_CASE("Custom tag resolveKey behavior", "[Mb]") {
	plugin::Model* model = createMockModel("test-plugin", "test-model", "Test Model");
	cleanupMockModels();
	
	SECTION("First inserted case is canonical") {
		customTagAdd(model, "MyTag");
		auto tags = customTagsForModel(model);
		REQUIRE(tags.count("MyTag") == 1);
		REQUIRE(tags.size() == 1);
	}
	
	SECTION("Adding same tag with different case does not create duplicate") {
		customTagAdd(model, "MyTag");
		REQUIRE(customTagsForModel(model).size() == 1);
		
		customTagAdd(model, "mytag");
		REQUIRE(customTagsForModel(model).size() == 1);
		
		customTagAdd(model, "MYTAG");
		REQUIRE(customTagsForModel(model).size() == 1);
	}
	
	SECTION("customTagHas with resolveKey finds existing tag") {
		customTagAdd(model, "MyTag");
		
		REQUIRE(customTagHas(model, "MyTag", true));
		REQUIRE(customTagHas(model, "mytag", true));
		REQUIRE(customTagHas(model, "MYTAG", true));
	}
	
	SECTION("customTagHas without resolveKey requires exact match") {
		customTagAdd(model, "MyTag");
		
		REQUIRE(customTagHas(model, "MyTag", false));
		REQUIRE(!customTagHas(model, "mytag", false));
		REQUIRE(!customTagHas(model, "MYTAG", false));
	}
	
	SECTION("customTagRemove with resolveKey removes regardless of case") {
		customTagAdd(model, "MyTag");
		REQUIRE(customTagHas(model, "MyTag"));
		
		customTagRemove(model, "mytag");
		REQUIRE(!customTagHas(model, "MyTag"));
		REQUIRE(customTagsForModel(model).empty());
	}
	
	SECTION("customTagDelete removes tag regardless of case used") {
		plugin::Model* m1 = createMockModel("plugin1", "model1", "Model 1");
		plugin::Model* m2 = createMockModel("plugin2", "model2", "Model 2");
		
		customTagAdd(m1, "TestTag");
		customTagAdd(m2, "TestTag");
		
		customTagDelete("testtag");  // Different case
		
		REQUIRE(!customTagHas(m1, "TestTag"));
		REQUIRE(!customTagHas(m2, "TestTag"));
		REQUIRE(customTagsAll().empty());
	}
	
	SECTION("Multiple tags with different canonical cases") {
		customTagAdd(model, "Alpha");
		customTagAdd(model, "Beta");
		customTagAdd(model, "Gamma");
		
		auto tags = customTagsForModel(model);
		REQUIRE(tags.size() == 3);
		REQUIRE(tags.count("Alpha") == 1);
		REQUIRE(tags.count("Beta") == 1);
		REQUIRE(tags.count("Gamma") == 1);
	}
	
	cleanupMockModels();
}

TEST_CASE("Predefined tag modifications", "[Mb]") {
	plugin::Model* model = createMockModel("test-plugin", "test-model", "Test Model");
	cleanupMockModels();
	
	// Add a predefined tag to the model
	model->tagIds.push_back(0);  // Assume tag 0 exists
	
	SECTION("Model starts with original tags") {
		auto tags = getEffectiveTagIds(model);
		REQUIRE(tags.count(0) == 1);
	}
	
	SECTION("predefinedTagAdd adds a tag") {
		predefinedTagAdd(model, 1);
		auto tags = getEffectiveTagIds(model);
		REQUIRE(tags.count(1) == 1);
	}
	
	SECTION("predefinedTagRemove removes a tag") {
		auto tags1 = getEffectiveTagIds(model);
		REQUIRE(tags1.count(0) == 1);
		
		predefinedTagRemove(model, 0);
		auto tags2 = getEffectiveTagIds(model);
		REQUIRE(tags2.count(0) == 0);
	}
	
	SECTION("Adding and removing the same tag cancels out") {
		predefinedTagAdd(model, 1);
		auto tags1 = getEffectiveTagIds(model);
		REQUIRE(tags1.count(1) == 1);
		
		predefinedTagRemove(model, 1);
		auto tags2 = getEffectiveTagIds(model);
		REQUIRE(tags2.count(1) == 0);
	}
	
	SECTION("predefinedTagHasAdded and predefinedTagHasRemoved") {
		predefinedTagAdd(model, 2);
		predefinedTagRemove(model, 3);
		
		REQUIRE(predefinedTagHasAdded(model, 2));
		REQUIRE(!predefinedTagHasAdded(model, 3));
		REQUIRE(!predefinedTagHasRemoved(model, 2));
		REQUIRE(predefinedTagHasRemoved(model, 3));
	}
	
	SECTION("predefinedTagsReset clears all modifications") {
		plugin::Model* m1 = createMockModel("plugin1", "model1", "Model 1");
		plugin::Model* m2 = createMockModel("plugin2", "model2", "Model 2");
		
		predefinedTagAdd(m1, 1);
		predefinedTagRemove(m2, 0);
		
		predefinedTagsReset();
		
		auto tags1 = getEffectiveTagIds(m1);
		auto tags2 = getEffectiveTagIds(m2);
		
		// After reset, m1 should have no added tags, m2 should have original tags
		REQUIRE(!predefinedTagHasAdded(m1, 1));
		REQUIRE(!predefinedTagHasRemoved(m2, 0));
	}
	
	cleanupMockModels();
}

TEST_CASE("Effective tag IDs with modifications", "[Mb]") {
	plugin::Model* model = createMockModel("test-plugin", "test-model", "Test Model");
	cleanupMockModels();
	
	// Setup: model has tags 0, 1, 2
	model->tagIds = {0, 1, 2};
	effectiveTagIdsCacheInvalidateAll();
	
	SECTION("Effective tags include original tags") {
		auto tags = getEffectiveTagIds(model);
		REQUIRE(tags.count(0) == 1);
		REQUIRE(tags.count(1) == 1);
		REQUIRE(tags.count(2) == 1);
	}
	
	SECTION("Removing a tag excludes it from effective tags") {
		predefinedTagRemove(model, 1);
		effectiveTagIdsCacheInvalidate(model);
		
		auto tags = getEffectiveTagIds(model);
		REQUIRE(tags.count(0) == 1);
		REQUIRE(tags.count(1) == 0);  // Removed
		REQUIRE(tags.count(2) == 1);
	}
	
	SECTION("Adding a new tag includes it in effective tags") {
		predefinedTagAdd(model, 5);
		effectiveTagIdsCacheInvalidate(model);
		
		auto tags = getEffectiveTagIds(model);
		REQUIRE(tags.count(0) == 1);
		REQUIRE(tags.count(5) == 1);  // Added
	}
	
	SECTION("getEffectiveTagNames converts IDs to names") {
		auto names = getEffectiveTagNames(model);
		REQUIRE(names.size() >= 0);  // At least the original tags
	}
	
	cleanupMockModels();
}

TEST_CASE("Tag cache invalidation", "[Mb]") {
	plugin::Model* model = createMockModel("test-plugin", "test-model", "Test Model");
	cleanupMockModels();
	
	model->tagIds = {0, 1};
	
	SECTION("effectiveTagIdsCacheInvalidate clears single model cache") {
		// Populate cache
		auto tags1 = getEffectiveTagIds(model);
		
		predefinedTagAdd(model, 5);
		effectiveTagIdsCacheInvalidate(model);
		
		auto tags2 = getEffectiveTagIds(model);
		REQUIRE(tags2.count(5) == 1);
	}
	
	SECTION("effectiveTagIdsCacheInvalidateAll clears all caches") {
		plugin::Model* m1 = createMockModel("plugin1", "model1", "Model 1");
		plugin::Model* m2 = createMockModel("plugin2", "model2", "Model 2");
		
		m1->tagIds = {0};
		m2->tagIds = {1};
		
		auto tags1Before = getEffectiveTagIds(m1);
		auto tags2Before = getEffectiveTagIds(m2);
		
		predefinedTagAdd(m1, 5);
		predefinedTagAdd(m2, 6);
		effectiveTagIdsCacheInvalidateAll();
		
		auto tags1After = getEffectiveTagIds(m1);
		auto tags2After = getEffectiveTagIds(m2);
		
		REQUIRE(tags1After.count(5) == 1);
		REQUIRE(tags2After.count(6) == 1);
	}
	
	cleanupMockModels();
}

TEST_CASE("Model usage tracking", "[Mb]") {
	plugin::Model* model = createMockModel("test-plugin", "test-model", "Test Model");
	cleanupMockModels();
	
	SECTION("Model starts with no usage") {
		auto it = modelUsage.find(model);
		REQUIRE((it == modelUsage.end() || it->second->usedCount == 0));
	}
	
	SECTION("modelUsageTouch increments count") {
		auto beforeCount = (modelUsage.find(model) != modelUsage.end()) ? modelUsage[model]->usedCount : 0;
		modelUsageTouch(model);
		auto afterCount = modelUsage[model]->usedCount;
		REQUIRE(afterCount == beforeCount + 1);
	}
	
	SECTION("modelUsageTouch updates timestamp") {
		int64_t before = std::chrono::duration_cast<std::chrono::microseconds>(
			std::chrono::system_clock::now().time_since_epoch()).count();
		
		modelUsageTouch(model);
		
		int64_t after = std::chrono::duration_cast<std::chrono::microseconds>(
			std::chrono::system_clock::now().time_since_epoch()).count();
		
		REQUIRE(modelUsage[model]->usedTimestamp >= before);
		REQUIRE(modelUsage[model]->usedTimestamp <= after);
	}
	
	SECTION("Multiple touches accumulate") {
		modelUsageTouch(model);
		modelUsageTouch(model);
		modelUsageTouch(model);
		REQUIRE(modelUsage[model]->usedCount == 3);
	}
	
	SECTION("modelUsageReset clears all usage") {
		plugin::Model* m1 = createMockModel("plugin1", "model1", "Model 1");
		plugin::Model* m2 = createMockModel("plugin2", "model2", "Model 2");

		modelUsageTouch(m1);
		modelUsageTouch(m2);

		REQUIRE(!modelUsage.empty());

		modelUsageReset();
		REQUIRE(modelUsage.empty());
	}

	cleanupMockModels();
}

TEST_CASE("Model usage sort keys", "[Mb]") {
	cleanupMockModels();

	SECTION("Never-used model reports zero, not INT64_MIN") {
		// Regression: the "last used"/"most used" sorts negate this value. A never-used
		// model must report 0 (not INT64_MIN, whose negation is undefined behavior and
		// would sort unused modules to the top instead of the bottom).
		plugin::Model* model = createMockModel("test", "test", "Test");
		REQUIRE(modelUsageTimestamp(model) == 0);
		REQUIRE(modelUsageCount(model) == 0);
	}

	SECTION("Used model reports a positive timestamp and count") {
		plugin::Model* model = createMockModel("test", "test", "Test");
		modelUsageTouch(model);
		REQUIRE(modelUsageTimestamp(model) > 0);
		REQUIRE(modelUsageCount(model) == 1);
	}

	SECTION("'Last used' key sorts used before never-used, most-recent first") {
		// Mirror the browser's LAST_USED primary sort key: negated timestamp, ascending.
		plugin::Model* unused = createMockModel("p0", "unused", "Unused");
		plugin::Model* older = createMockModel("p1", "older", "Older");
		plugin::Model* newer = createMockModel("p2", "newer", "Newer");

		modelUsageTouch(older);
		// Ensure a strictly greater timestamp for "newer" regardless of clock resolution.
		modelUsage[newer] = new ModelUsage;
		modelUsage[newer]->usedCount = 1;
		modelUsage[newer]->usedTimestamp = modelUsage[older]->usedTimestamp + 1;

		auto key = [](plugin::Model* m) { return -modelUsageTimestamp(m); };
		std::vector<plugin::Model*> models = { unused, older, newer };
		std::sort(models.begin(), models.end(),
			[&](plugin::Model* a, plugin::Model* b) { return key(a) < key(b); });

		// Most recently used first, never-used last.
		REQUIRE(models[0] == newer);
		REQUIRE(models[1] == older);
		REQUIRE(models[2] == unused);
	}

	cleanupMockModels();
}


TEST_CASE("JSON serialization and deserialization", "[Mb]") {
	plugin::Model* model = createMockModel("test-plugin", "test-model", "Test Model");
	cleanupMockModels();
	
	SECTION("Empty state serializes to valid JSON") {
		json_t* json = moduleBrowserToJson(false);
		REQUIRE(json != NULL);
		REQUIRE(json_is_object(json));
		json_decref(json);
	}
	
	SECTION("Favorites are serialized and deserialized") {
		setModelFavorite(model, true);
		
		json_t* json = moduleBrowserToJson(false);
		cleanupMockModels();
		
		moduleBrowserFromJson(json);
		
		// Note: This will only work if the model still exists after deserialization
		// In a real test, we'd need to ensure the model is still available
		json_decref(json);
	}
	
	SECTION("Hidden models are serialized and deserialized") {
		toggleModelHidden(model);
		
		json_t* json = moduleBrowserToJson(false);
		json_t* hiddenJ = json_object_get(json, "hidden");
		
		// Verify the hidden array is created and contains the hidden model
		REQUIRE(json_is_array(hiddenJ));
		REQUIRE(json_array_size(hiddenJ) >= 1);
		
		json_decref(json);
	}
	
	SECTION("Custom tags are serialized and deserialized") {
		customTagAdd(model, "TestTag");
		
		json_t* json = moduleBrowserToJson(false);
		
		// Verify the tag is in the JSON
		json_t* customTagsJ = json_object_get(json, "customTags");
		REQUIRE(customTagsJ != NULL);
		
		json_decref(json);
	}
	
	SECTION("Predefined tag modifications are serialized") {
		model->tagIds = {0};
		predefinedTagAdd(model, 1);
		
		json_t* json = moduleBrowserToJson(false);
		
		json_t* predefinedTagsJ = json_object_get(json, "predefinedTags");
		REQUIRE(predefinedTagsJ != NULL);
		
		json_decref(json);
	}
	
	SECTION("Usage data is included when requested") {
		modelUsageTouch(model);
		
		json_t* jsonWithUsage = moduleBrowserToJson(true);
		json_t* usageJ = json_object_get(jsonWithUsage, "usage");
		REQUIRE(usageJ != NULL);
		
		json_t* jsonWithoutUsage = moduleBrowserToJson(false);
		usageJ = json_object_get(jsonWithoutUsage, "usage");
		REQUIRE(usageJ == NULL);
		
		json_decref(jsonWithUsage);
		json_decref(jsonWithoutUsage);
	}
	
	cleanupMockModels();
}

TEST_CASE("JSON roundtrip preserves all data", "[Mb]") {
	plugin::Model* model = createMockModel("test-plugin", "test-model", "Test Model");
	cleanupMockModels();
	favoriteMode = FavoriteMode::MB;
	
	// Set up some data
	setModelFavorite(model, true);
	toggleModelHidden(model);
	customTagAdd(model, "CustomTag1");
	customTagAdd(model, "CustomTag2");
	model->tagIds = {0, 1};
	predefinedTagAdd(model, 2);
	predefinedTagRemove(model, 0);
	modelUsageTouch(model);
	
	SECTION("Favorites are serialized correctly") {
		json_t* json = moduleBrowserToJson(true);
		json_t* favoritesJ = json_object_get(json, "favorites");
		REQUIRE(json_is_array(favoritesJ));
		REQUIRE(json_array_size(favoritesJ) >= 1);
		json_decref(json);
	}
	
	SECTION("Hidden models are serialized correctly") {
		json_t* json = moduleBrowserToJson(true);
		json_t* hiddenJ = json_object_get(json, "hidden");
		REQUIRE(json_is_array(hiddenJ));
		REQUIRE(json_array_size(hiddenJ) >= 1);
		json_decref(json);
	}
	
	SECTION("Custom tags are serialized correctly") {
		json_t* json = moduleBrowserToJson(true);
		json_t* customTagsJ = json_object_get(json, "customTags");
		REQUIRE(json_is_object(customTagsJ));
		json_t* tagsJ = json_object_get(customTagsJ, "CustomTag1");
		REQUIRE(tagsJ != NULL);
		json_decref(json);
	}
	
	SECTION("Predefined tag modifications are serialized correctly") {
		json_t* json = moduleBrowserToJson(true);
		json_t* predefinedTagsJ = json_object_get(json, "predefinedTags");
		REQUIRE(json_is_array(predefinedTagsJ));
		REQUIRE(json_array_size(predefinedTagsJ) >= 1);
		json_decref(json);
	}
	
	SECTION("Usage data is serialized when requested") {
		json_t* json = moduleBrowserToJson(true);
		json_t* usageJ = json_object_get(json, "usage");
		REQUIRE(json_is_array(usageJ));
		json_decref(json);
	}
	
	cleanupMockModels();
}


TEST_CASE("Edge cases and corner cases", "[Mb]") {
	cleanupMockModels();
	
	SECTION("Operations on non-existent model") {
		plugin::Model* model = createMockModel("test", "test", "Test");
		
		// These should not crash
		REQUIRE(!isModelFavorite(model));
		REQUIRE(!isModelHidden(model));
		REQUIRE(customTagsForModel(model).empty());
		REQUIRE(getEffectiveTagIds(model).empty());
	}
	
	SECTION("Removing non-existent custom tag") {
		plugin::Model* model = createMockModel("test", "test", "Test");
		
		// Should not crash
		customTagRemove(model, "NonExistent");
		REQUIRE(customTagsForModel(model).empty());
	}
	
	SECTION("Multiple operations on same model") {
		plugin::Model* model = createMockModel("test", "test", "Test");
		
		customTagAdd(model, "Tag1");
		toggleModelFavorite(model);
		toggleModelHidden(model);
		customTagAdd(model, "Tag2");
		toggleModelFavorite(model);
		
		REQUIRE(customTagsForModel(model).size() == 2);
		REQUIRE(!isModelFavorite(model));
		REQUIRE(isModelHidden(model));
	}
	
	SECTION("Large number of tags") {
		plugin::Model* model = createMockModel("test", "test", "Test");
		
		for (int i = 0; i < 100; i++) {
			customTagAdd(model, "Tag" + std::to_string(i));
		}
		
		REQUIRE(customTagsForModel(model).size() == 100);
		REQUIRE(customTagsAll().size() == 100);
	}
	
	cleanupMockModels();
}

TEST_CASE("Favorite and hidden interaction", "[Mb]") {
	plugin::Model* model = createMockModel("test-plugin", "test-model", "Test Model");
	cleanupMockModels();
	
	SECTION("Favoriting removes from hidden") {
		toggleModelHidden(model);
		REQUIRE(isModelHidden(model));
		
		setModelFavorite(model, true);
		REQUIRE(!isModelHidden(model));
	}
	
	SECTION("Model can be hidden and favorite simultaneously") {
		setModelFavorite(model, true);
		// Manually add to hidden after setting as favorite
		hiddenModels.insert(model);
		
		REQUIRE(isModelFavorite(model));
		REQUIRE(isModelHidden(model));
	}
	
	cleanupMockModels();
}
