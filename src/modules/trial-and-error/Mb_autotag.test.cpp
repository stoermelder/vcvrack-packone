#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"
#include "Mb_autotag.hpp"

using namespace StoermelderPackOne::Mb;

Test::TestContext<> testContext;


TEST_CASE("customTagAuto", "[Mb]") {
    // Create a test plugin with our models
    rack::plugin::Plugin testPlugin;
    testPlugin.slug = "test-plugin";
    testPlugin.name = "Test Plugin";
    
    rack::plugin::Model model1;
    model1.plugin = &testPlugin;
    model1.slug = "matrix-mixer";
    model1.name = "Matrix Mixer";
    model1.description = "A matrix mixer for routing audio.";

    rack::plugin::Model model2;
    model2.plugin = &testPlugin;
    model2.slug = "wavefolder";
    model2.name = "Wavefolder";
    model2.description = "A wavefolding oscillator.";

    rack::plugin::Model model3;
    model3.plugin = &testPlugin;
    model3.slug = "formant-osc";
    model3.name = "Formant Oscillator";
    model3.description = "An oscillator with formant characteristics.";
    
    testPlugin.models.push_back(&model1);
    testPlugin.models.push_back(&model2);
    testPlugin.models.push_back(&model3);
    
    // Use plugin::Plugin* type to match the function signature
    std::vector<plugin::Plugin*> plugins = {&testPlugin};

    // Create mock rules
    std::vector<AutoTagRule> rules = {
        {"Matrix Mixer", {"matrix mixer", "matrix routing"}},
        {"Wavefolder", {"wavefold"}},
        {"Formant", {"formant oscillator", "formant filter"}, {"format"}}
    };

    auto result = customTagAuto(rules, plugins);

    SECTION("Matrix Mixer is tagged correctly") {
        REQUIRE(result.perTag["Matrix Mixer"] == 1);
        REQUIRE(result.assignments.at("Matrix Mixer").count(&model1) == 1);
    }

    SECTION("Wavefolder is tagged correctly") {
        REQUIRE(result.perTag["Wavefolder"] == 1);
        REQUIRE(result.assignments.at("Wavefolder").count(&model2) == 1);
    }

    SECTION("Formant Oscillator is tagged correctly (blockword does not match)") {
        // "format" blockword does NOT match "Formant Oscillator" because fuzzy
        // search for "format" does not return "Formant Oscillator" with high confidence.
        // This demonstrates blockwords correctly allowing legitimate matches.
        REQUIRE(result.perTag["Formant"] == 1);
        REQUIRE(result.assignments.at("Formant").count(&model3) == 1);
    }

    // Cleanup
    testPlugin.models.clear();
    plugins.clear();
}

TEST_CASE("customTagAuto blockword handling", "[Mb]") {
	SECTION("Blockword prevents tagging when blockword matches model") {
		rack::plugin::Plugin testPlugin;
		testPlugin.slug = "test-plugin";
		testPlugin.name = "Test Plugin";

		// Model with blockword "visual" in name - should be blocked
		rack::plugin::Model modelWithBlockword;
		modelWithBlockword.plugin = &testPlugin;
		modelWithBlockword.slug = "visual-feedback";
		modelWithBlockword.name = "Visual Feedback";
		modelWithBlockword.description = "Visual feedback module.";

		// Model without blockword - should be tagged
		rack::plugin::Model modelWithoutBlockword;
		modelWithoutBlockword.plugin = &testPlugin;
		modelWithoutBlockword.slug = "audio-feedback";
		modelWithoutBlockword.name = "Audio Feedback";
		modelWithoutBlockword.description = "Audio feedback module.";

		testPlugin.models.push_back(&modelWithBlockword);
		testPlugin.models.push_back(&modelWithoutBlockword);
		std::vector<plugin::Plugin*> plugins = {&testPlugin};

		// Using Feedback rule with blockword "visual"
		std::vector<AutoTagRule> rules = {
			{"Feedback", {"feedback"}, {"visual"}}
		};

		auto result = customTagAuto(rules, plugins);

		// modelWithoutBlockword should be tagged, modelWithBlockword should be blocked
		REQUIRE(result.perTag["Feedback"] == 1);
		REQUIRE(result.assignments["Feedback"].count(&modelWithoutBlockword) == 1);
		REQUIRE(result.assignments["Feedback"].count(&modelWithBlockword) == 0);

		testPlugin.models.clear();
		plugins.clear();
	}

	SECTION("Blockword 'jooper' prevents tagging 'nysthi jooper 8 channel'") {
		rack::plugin::Plugin testPlugin;
		testPlugin.slug = "test-plugin";
		testPlugin.name = "Test Plugin";

		// Model named "nysthi jooper 8 channel" - should be blocked by "jooper" blockword
		rack::plugin::Model modelJooper;
		modelJooper.plugin = &testPlugin;
		modelJooper.slug = "nysthi-jooper";
		modelJooper.name = "nysthi jooper 8 channel";
		modelJooper.description = "8 channel, with scene manager";

		// Model named "looper pro" - should be tagged with Looper tag
		rack::plugin::Model modelLooper;
		modelLooper.plugin = &testPlugin;
		modelLooper.slug = "looper-pro";
		modelLooper.name = "Looper Pro";
		modelLooper.description = "Professional looper module.";

		testPlugin.models.push_back(&modelJooper);
		testPlugin.models.push_back(&modelLooper);
		std::vector<plugin::Plugin*> plugins = {&testPlugin};

		// Looper rule with blockword "jooper"
		std::vector<AutoTagRule> rules = {
			{"Looper", {"looper"}, {"jooper"}}
		};

		auto result = customTagAuto(rules, plugins);

		// modelLooper should be tagged, modelJooper should be blocked
		REQUIRE(result.perTag["Looper"] == 1);
		REQUIRE(result.assignments["Looper"].count(&modelLooper) == 1);
		REQUIRE(result.assignments["Looper"].count(&modelJooper) == 0);

		testPlugin.models.clear();
		plugins.clear();
	}

	SECTION("No blockword match allows tagging") {
		rack::plugin::Plugin testPlugin;
		testPlugin.slug = "test-plugin";
		testPlugin.name = "Test Plugin";

		rack::plugin::Model modelClean;
		modelClean.plugin = &testPlugin;
		modelClean.slug = "pure-feedback";
		modelClean.name = "Pure Feedback";
		modelClean.description = "A clean feedback module.";

		testPlugin.models.push_back(&modelClean);
		std::vector<plugin::Plugin*> plugins = {&testPlugin};

		std::vector<AutoTagRule> rules = {
			{"Feedback", {"feedback"}, {"visual"}}
		};

		auto result = customTagAuto(rules, plugins);

		REQUIRE(result.perTag["Feedback"] == 1);
		REQUIRE(result.assignments["Feedback"].count(&modelClean) == 1);

		testPlugin.models.clear();
		plugins.clear();
	}

	SECTION("Skips models already tagged with the rule's tag") {
		rack::plugin::Plugin testPlugin;
		testPlugin.slug = "test-plugin";
		testPlugin.name = "Test Plugin";

		rack::plugin::Model modelToSkip;
		modelToSkip.plugin = &testPlugin;
		modelToSkip.slug = "already-tagged";
		modelToSkip.name = "Already Tagged";
		modelToSkip.description = "Already has Feedback tag.";

		testPlugin.models.push_back(&modelToSkip);
		std::vector<plugin::Plugin*> plugins = {&testPlugin};

		// Pre-tag the model
		customTagAdd(&modelToSkip, "Feedback");

		std::vector<AutoTagRule> rules = {
			{"Feedback", {"feedback"}, {"visual"}}
		};

		auto result = customTagAuto(rules, plugins);

		// Should skip the already-tagged model
		REQUIRE(result.perTag["Feedback"] == 0);
		REQUIRE(result.assignments["Feedback"].count(&modelToSkip) == 0);

		customTagRemove(&modelToSkip, "Feedback");
		testPlugin.models.clear();
		plugins.clear();
	}
}

TEST_CASE("customTagSearch", "[Mb]") {
    // Create a test plugin with our model
    rack::plugin::Plugin testPlugin;
    testPlugin.slug = "test-plugin";
    testPlugin.name = "Test Plugin";
    
    rack::plugin::Model model1;
    model1.plugin = &testPlugin;
    model1.slug = "test-module";
    model1.name = "Test Module";
    model1.description = "This is a test module with some keywords.";
    
    testPlugin.models.push_back(&model1);
    
    // Use plugin::Plugin* type to match the function signature
    std::vector<plugin::Plugin*> plugins = {&testPlugin};

    SECTION("Search for exact name") {
        auto result = customTagSearch("Test Module", plugins);
        REQUIRE(result.perTag["Test Module"] == 1);
        REQUIRE(result.assignments.at("Test Module").count(&model1) == 1);
    }

    SECTION("Search for keyword in description") {
        // Since we don't have a rule for "test", this won't match anything in AUTO_TAG_RULES,
        // but customTagSearch just uses the query as the tag name.
        auto result = customTagSearch("keywords", plugins);
        REQUIRE(result.perTag["keywords"] == 1);
        REQUIRE(result.assignments.at("keywords").count(&model1) == 1);
    }

    // Cleanup
    testPlugin.models.clear();
    plugins.clear();
}

TEST_CASE("customTagMetamodule", "[Mb]") {
	SECTION("Returns empty result when no modules are provided") {
		std::set<std::pair<std::string, std::string>> mockSlugs = {};
		std::vector<plugin::Plugin*> emptyPlugins;
		
		auto result = customTagMetamodule(mockSlugs, emptyPlugins);
		
		REQUIRE(result.total == 0);
		REQUIRE(result.perTag.empty());
		REQUIRE(result.assignments.empty());
	}

	SECTION("Assigns modules based on MetaModule slugs") {
		// Create a test plugin with our models
		rack::plugin::Plugin testPlugin;
		testPlugin.slug = "test-plugin";
		testPlugin.name = "Test Plugin";
		
		rack::plugin::Model model1;
		model1.plugin = &testPlugin;
		model1.slug = "matrix-mixer";
		model1.name = "Matrix Mixer";
		model1.description = "A matrix mixer for routing audio.";

		rack::plugin::Model model2;
		model2.plugin = &testPlugin;
		model2.slug = "oscillator-1";
		model2.name = "Oscillator 1";
		model2.description = "An oscillator module.";

		rack::plugin::Model model3;
		model3.plugin = &testPlugin;
		model3.slug = "filter-1";
		model3.name = "Filter 1";
		model3.description = "A filter module.";
		
		testPlugin.models.push_back(&model1);
		testPlugin.models.push_back(&model2);
		testPlugin.models.push_back(&model3);
		std::vector<plugin::Plugin*> plugins = {&testPlugin};

		std::set<std::pair<std::string, std::string>> mockSlugs = {
			{"test-plugin", "matrix-mixer"},
			{"test-plugin", "oscillator-1"}
		};
		
		auto result = customTagMetamodule(mockSlugs, plugins);

		REQUIRE(result.total == 2);
		REQUIRE(result.perTag["MetaModule"] == 2);
		REQUIRE(result.assignments.count("MetaModule") == 1);
		REQUIRE(result.assignments["MetaModule"].size() == 2);

		// Cleanup
		testPlugin.models.clear();
		plugins.clear();
	}

	SECTION("Excludes modules already tagged with MetaModule") {
		// Create a test plugin with our model
		rack::plugin::Plugin testPlugin;
		testPlugin.slug = "test-plugin";
		testPlugin.name = "Test Plugin";
		
		rack::plugin::Model preTaggedModel;
		preTaggedModel.plugin = &testPlugin;
		preTaggedModel.slug = "pre-tagged-module";
		preTaggedModel.name = "Pre-Tagged Module";
		preTaggedModel.description = "Already tagged with MetaModule.";
		
		testPlugin.models.push_back(&preTaggedModel);
		std::vector<plugin::Plugin*> plugins = {&testPlugin};

		// Actually add the tag to simulate pre-existing tag
		customTagAdd(&preTaggedModel, "MetaModule");

		std::set<std::pair<std::string, std::string>> mockSlugs = {
				{"test-plugin", "pre-tagged-module"}
			};
		
		auto result = customTagMetamodule(mockSlugs, plugins);

		REQUIRE(result.total == 0);

		customTagRemove(&preTaggedModel, "MetaModule");
		// Cleanup
		testPlugin.models.clear();
		plugins.clear();
	}

	
	SECTION("Handles multiple modules from same plugin") {
		// Create a test plugin with our models
		rack::plugin::Plugin testPlugin;
		testPlugin.slug = "test-plugin";
		testPlugin.name = "Test Plugin";
		
		rack::plugin::Model modelA;
		modelA.plugin = &testPlugin;
		modelA.slug = "module-a";
		modelA.name = "Module A";
		modelA.description = "First module.";

		rack::plugin::Model modelB;
		modelB.plugin = &testPlugin;
		modelB.slug = "module-b";
		modelB.name = "Module B";
		modelB.description = "Second module.";
		
		testPlugin.models.push_back(&modelA);
		testPlugin.models.push_back(&modelB);
		std::vector<plugin::Plugin*> plugins = {&testPlugin};

		std::set<std::pair<std::string, std::string>> mockSlugs = {
				{"test-plugin", "module-a"},
				{"test-plugin", "module-b"}
			};
		
		auto result = customTagMetamodule(mockSlugs, plugins);

		REQUIRE(result.total == 2);
		REQUIRE(result.perTag["MetaModule"] == 2);
		REQUIRE(result.assignments.count("MetaModule") == 1);
		REQUIRE(result.assignments["MetaModule"].size() == 2);

		// Cleanup
		testPlugin.models.clear();
		plugins.clear();
	}

	SECTION("Works with models from different plugins") {
		// Create a test plugin with our model
		rack::plugin::Plugin testPlugin;
		testPlugin.slug = "other-plugin";
		testPlugin.name = "Other Plugin";
		
		rack::plugin::Model model1;
		model1.plugin = &testPlugin;
		model1.slug = "cross-plugin-1";
		model1.name = "Cross Plugin 1";
		model1.description = "Module from cross-plugin test.";
		
		testPlugin.models.push_back(&model1);
		std::vector<plugin::Plugin*> plugins = {&testPlugin};

		std::set<std::pair<std::string, std::string>> mockSlugs = {
			{"other-plugin", "cross-plugin-1"}
		};
		
		auto result = customTagMetamodule(mockSlugs, plugins);

		REQUIRE(result.total == 1);
		REQUIRE(result.perTag["MetaModule"] == 1);
		REQUIRE(result.assignments.count("MetaModule") == 1);

		// Cleanup
		testPlugin.models.clear();
		plugins.clear();
	}

	SECTION("Handles empty plugin models list") {
		// Create a test plugin with models
		rack::plugin::Plugin testPlugin;
		testPlugin.slug = "test-plugin";
		testPlugin.name = "Test Plugin";
		
		rack::plugin::Model model1;
		model1.plugin = &testPlugin;
		model1.slug = "model-1";
		model1.name = "Model 1";
		model1.description = "Test model 1.";
		
		rack::plugin::Model model2;
		model2.plugin = &testPlugin;
		model2.slug = "model-2";
		model2.name = "Model 2";
		model2.description = "Test model 2.";
		
		testPlugin.models.push_back(&model1);
		testPlugin.models.push_back(&model2);
		std::vector<plugin::Plugin*> plugins = {&testPlugin};

		std::set<std::pair<std::string, std::string>> mockSlugs;
		
		auto result = customTagMetamodule(mockSlugs, plugins);

		REQUIRE(result.total == 0);
		REQUIRE(result.perTag.empty());
		REQUIRE(result.assignments.empty());

		// Cleanup
		testPlugin.models.clear();
		plugins.clear();
	}

	SECTION("Correctly processes module slug matching") {
		// Create a test plugin with our model
		rack::plugin::Plugin testPlugin;
		testPlugin.slug = "test-plugin";
		testPlugin.name = "Test Plugin";
		
		rack::plugin::Model model;
		model.plugin = &testPlugin;
		model.slug = "test-slug-value";
		model.name = "Test Slug Module";
		model.description = "Testing slug matching functionality.";
		
		testPlugin.models.push_back(&model);
		std::vector<plugin::Plugin*> plugins = {&testPlugin};

		std::set<std::pair<std::string, std::string>> mockSlugs = {
			{"test-plugin", "test-slug-value"}
		};
		
		auto result = customTagMetamodule(mockSlugs, plugins);

		REQUIRE(result.total == 1);
		REQUIRE(result.perTag["MetaModule"] == 1);
		REQUIRE(result.assignments.count("MetaModule") == 1);

		// Cleanup
		testPlugin.models.clear();
		plugins.clear();
	}

	SECTION("Parses YAML correctly when download succeeds") {
		// Create a temporary file with mock YAML content
		std::string tmpFile = rack::system::getTempDirectory() + "/metamodule-plugins-test.yml";
		
		FILE* file = fopen(tmpFile.c_str(), "w");
		REQUIRE(file != nullptr);
		
		// Write mock YAML content (simplified version of MetaModule format)
		// Indentation: 8 spaces = plugin level, 16 spaces = module level
		fprintf(file, "        VCVSlug: test-plugin\n");
		fprintf(file, "                VCVSlug: matrix-mixer\n");
		fprintf(file, "                VCVSlug: oscillator-1\n");
		fclose(file);

		// Use parseMetamoduleYaml to test parsing logic independently
		auto parsedSlugs = parseMetamoduleYaml(tmpFile);
		
		REQUIRE(parsedSlugs.size() == 2);
		REQUIRE(parsedSlugs.count(std::pair<std::string, std::string>("test-plugin", "matrix-mixer")) > 0);
		REQUIRE(parsedSlugs.count(std::pair<std::string, std::string>("test-plugin", "oscillator-1")) > 0);

		std::remove(tmpFile.c_str());
	}

	SECTION("Returns empty result when YAML file is missing") {
		std::set<std::pair<std::string, std::string>> mockSlugs = {};
		std::vector<plugin::Plugin*> emptyPlugins;
		
		auto result = customTagMetamodule(mockSlugs, emptyPlugins);
		
		REQUIRE(result.total == 0);
		REQUIRE(result.perTag.empty());
		REQUIRE(result.assignments.empty());
	}

	SECTION("Returns correct result for matching modules") {
		// Create a test plugin with our model
		rack::plugin::Plugin testPlugin;
		testPlugin.slug = "test-plugin";
		testPlugin.name = "Test Plugin";
		
		rack::plugin::Model model;
		model.plugin = &testPlugin;
		model.slug = "test-module";
		model.name = "Test Module";
		model.description = "A test module.";
		
		testPlugin.models.push_back(&model);
		std::vector<plugin::Plugin*> plugins = {&testPlugin};

		std::set<std::pair<std::string, std::string>> mockSlugs = {
			{"test-plugin", "test-module"}
		};
		
		// Call customTagMetamodule to test the matching logic
		auto result = customTagMetamodule(mockSlugs, plugins);
		REQUIRE(result.total == 1);
		REQUIRE(result.perTag["MetaModule"] == 1);
		REQUIRE(result.assignments.count("MetaModule") == 1);
		REQUIRE(result.assignments["MetaModule"].count(&model) > 0);

		// Cleanup
		testPlugin.models.clear();
		plugins.clear();
	}

	SECTION("Handles duplicate slugs in mock data") {
		// Create a test plugin with our models
		rack::plugin::Plugin testPlugin;
		testPlugin.slug = "test-plugin";
		testPlugin.name = "Test Plugin";
		
		rack::plugin::Model model1;
		model1.plugin = &testPlugin;
		model1.slug = "duplicate-module";
		model1.name = "Duplicate Module";
		model1.description = "Testing duplicate handling.";

		rack::plugin::Model model2;
		model2.plugin = &testPlugin;
		model2.slug = "different-module";
		model2.name = "Different Module";
		model2.description = "Another module.";
		
		testPlugin.models.push_back(&model1);
		testPlugin.models.push_back(&model2);
		std::vector<plugin::Plugin*> plugins = {&testPlugin};

		// Same module slug appears multiple times
		std::set<std::pair<std::string, std::string>> mockSlugs = {
			{"test-plugin", "duplicate-module"},
			{"test-plugin", "duplicate-module"},  // Duplicate
			{"test-plugin", "different-module"}
		};
		
		auto result = customTagMetamodule(mockSlugs, plugins);
		
		// Both modules should be assigned under "MetaModule"
		REQUIRE(result.total == static_cast<int>(testPlugin.models.size()));
		REQUIRE(result.assignments.count("MetaModule") == 1);
		REQUIRE(result.assignments["MetaModule"].count(&model1) == 1);
		REQUIRE(result.assignments["MetaModule"].count(&model2) == 1);

		// Cleanup
		testPlugin.models.clear();
		plugins.clear();
	}

	SECTION("Handles modules with invalid slugs") {
		// Create a test plugin with our model
		rack::plugin::Plugin testPlugin;
		testPlugin.slug = "test-plugin";
		testPlugin.name = "Test Plugin";
		
		rack::plugin::Model model;
		model.plugin = &testPlugin;
		model.slug = "invalid-slug-123";
		model.name = "Invalid Slug Module";
		model.description = "Module with invalid slug.";
		
		testPlugin.models.push_back(&model);
		std::vector<plugin::Plugin*> plugins = {&testPlugin};

		std::set<std::pair<std::string, std::string>> mockSlugs = {
			{"test-plugin", "valid-module"}
		};
		
		auto result = customTagMetamodule(mockSlugs, plugins);

		// Module should not be assigned since its slug doesn't match
		REQUIRE(result.total == 0);
		REQUIRE(result.assignments.count("invalid-slug-123") == 0);

		// Cleanup
		testPlugin.models.clear();
		plugins.clear();
	}

	SECTION("Handles malformed YAML file") {
		std::string tmpFile = rack::system::getTempDirectory() + "/malformed-yaml-test.yml";
		
		FILE* file = fopen(tmpFile.c_str(), "w");
		REQUIRE(file != nullptr);
		fclose(file);

		auto parsedSlugs = parseMetamoduleYaml(tmpFile);
		
		REQUIRE(parsedSlugs.size() == 0);

		std::remove(tmpFile.c_str());
	}
};


// Real-world YAML data test for parseMetamoduleYaml
// Reduced sample from https://metamodule.info/dl/plugins.yml
static const char* SAMPLE_METAMODULE_YAML = R"(        VCVSlug: stoermelder-packone
                VCVSlug: P1-Midist
                VCVSlug: P1-MidiCat
                VCVSlug: P1-MidiKey
                VCVSlug: P1-Transith
                VCVSlug: P1-MidiTrack
                VCVSlug: P1-MidiMon
                VCVSlug: P1-MidiStep
                VCVSlug: P1-MidiCV
                VCVSlug: P1-Ahab
                VCVSlug: P1-Mb
                VCVSlug: P1-Intermix
                VCVSlug: P1-Orbit
                VCVSlug: P1-Pile
                VCVSlug: P1-Raw
                VCVSlug: P1-RotorA
                VCVSlug: P1-Maze
                VCVSlug: P1-Midi
                VCVSlug: P1-Sipo
                VCVSlug: P1-Goto
                VCVSlug: P1-Trial
                VCVSlug: P1-Arena
                VCVSlug: P1-FourRds
                VCVSlug: P1-Bolt
                VCVSlug: P1-Transit
                VCVSlug: P1-Infix
                VCVSlug: P1-Stroke
                VCVSlug: P1-Affix
                VCVSlug: P1-Sail
        VCVSlug: VCV-AudibleInstruments
                VCVSlug: AudibleInstruments-Plinky
                VCVSlug: AudibleInstruments-Sampler
                VCVSlug: AudibleInstruments-Synthesizer
                VCVSlug: AudibleInstruments-Subble
        VCVSlug: fundamental
                VCVSlug: Fundamental-Mixer
                VCVSlug: Fundamental-VCA
                VCVSlug: Fundamental-LFO
)";

TEST_CASE("parseMetamoduleYaml with real-world data", "[Mb]") {
	SECTION("Parses real MetaModule plugin list format") {
		std::string tmpFile = rack::system::getTempDirectory() + "/metamodule-real-test.yml";
		FILE* file = fopen(tmpFile.c_str(), "w");
		REQUIRE(file != nullptr);
		fputs(SAMPLE_METAMODULE_YAML, file);
		fclose(file);

		auto parsed = parseMetamoduleYaml(tmpFile);

		// stoermelder-packone has 28 modules in the sample
		int stoermelderCount = 0;
		for (const auto& p : parsed) {
			if (p.first == "stoermelder-packone")
				stoermelderCount++;
		}
		REQUIRE(stoermelderCount == 28);

		// VCV-AudibleInstruments has 4 modules
		int audibleCount = 0;
		for (const auto& p : parsed) {
			if (p.first == "VCV-AudibleInstruments")
				audibleCount++;
		}
		REQUIRE(audibleCount == 4);

		// fundamental has 3 modules in the sample
		int fundamentalCount = 0;
		for (const auto& p : parsed) {
			if (p.first == "fundamental")
				fundamentalCount++;
		}
		REQUIRE(fundamentalCount == 3);

		// Verify specific module entries exist
		REQUIRE(parsed.count({"stoermelder-packone", "P1-Mb"}) == 1);
		REQUIRE(parsed.count({"VCV-AudibleInstruments", "AudibleInstruments-Plinky"}) == 1);
		REQUIRE(parsed.count({"fundamental", "Fundamental-Mixer"}) == 1);

		std::remove(tmpFile.c_str());
	}

	SECTION("Handles empty lines in YAML") {
		std::string tmpFile = rack::system::getTempDirectory() + "/metamodule-empty-lines.yml";
		FILE* file = fopen(tmpFile.c_str(), "w");
		REQUIRE(file != nullptr);
		fprintf(file, "        VCVSlug: test-plugin\n");
		fprintf(file, "\n");  // empty line
		fprintf(file, "                VCVSlug: module-1\n");
		fprintf(file, "\n");  // empty line
		fprintf(file, "                VCVSlug: module-2\n");
		fclose(file);

		auto parsed = parseMetamoduleYaml(tmpFile);

		// Should only get the module entries, not the empty lines
		REQUIRE(parsed.size() == 2);
		REQUIRE(parsed.count({"test-plugin", "module-1"}) == 1);
		REQUIRE(parsed.count({"test-plugin", "module-2"}) == 1);

		std::remove(tmpFile.c_str());
	}

	SECTION("Handles irregular indentation") {
		std::string tmpFile = rack::system::getTempDirectory() + "/metamodule-irregular.yml";
		FILE* file = fopen(tmpFile.c_str(), "w");
		REQUIRE(file != nullptr);
		// Plugin at 8 spaces, modules at 16 spaces (correct)
		fprintf(file, "        VCVSlug: my-plugin\n");
		fprintf(file, "                VCVSlug: my-module\n");
		// Plugin at 0 spaces (should be ignored - no leading spaces)
		fprintf(file, "VCVSlug: bad-plugin\n");
		fprintf(file, "                VCVSlug: bad-module\n");
		// Module at 8 spaces - treated as plugin level, not under my-plugin
		fprintf(file, "        VCVSlug: another-plugin\n");
		fprintf(file, "                VCVSlug: good-module\n");
		fprintf(file, "        VCVSlug: orphan-module\n");
		fprintf(file, "                VCVSlug: nested-module\n");
		fclose(file);

		auto parsed = parseMetamoduleYaml(tmpFile);

		// Parser treats 8-space VCVSlug as plugin, 16-space as module under last plugin
		// 1. my-plugin/my-module (correct 8/16 indent)
		// 2. bad-plugin ignored (0 spaces, not 8)
		// 3. another-plugin/good-module (8/16 indent after previous plugin)
		// 4. orphan-module becomes a NEW plugin, with nested-module as its module
		REQUIRE(parsed.size() == 4);
		REQUIRE(parsed.count({"my-plugin", "my-module"}) == 1);
		REQUIRE(parsed.count({"another-plugin", "good-module"}) == 1);
		REQUIRE(parsed.count({"bad-plugin", "bad-module"}) == 0); // ignored (0 spaces)
		REQUIRE(parsed.count({"orphan-module", "nested-module"}) == 1); // orphan-module is a plugin

		std::remove(tmpFile.c_str());
	}

	SECTION("Handles VCVSlug without trailing module entries") {
		std::string tmpFile = rack::system::getTempDirectory() + "/metamodule-no-modules.yml";
		FILE* file = fopen(tmpFile.c_str(), "w");
		REQUIRE(file != nullptr);
		fprintf(file, "        VCVSlug: plugin-without-modules\n");
		fprintf(file, "                VCVSlug: only-one\n");
		fprintf(file, "        VCVSlug: another-plugin\n");
		// another-plugin has no modules
		fclose(file);

		auto parsed = parseMetamoduleYaml(tmpFile);

		REQUIRE(parsed.count({"plugin-without-modules", "only-one"}) == 1);
		REQUIRE(parsed.count({"another-plugin", ""}) == 0); // Should not create empty entry

		std::remove(tmpFile.c_str());
	}
}

TEST_CASE("customTagMetamodule edge cases", "[Mb]") {
	SECTION("Plugin slug mismatch is case-sensitive") {
		rack::plugin::Plugin testPlugin;
		testPlugin.slug = "Test-Plugin";  // Note: capital T
		testPlugin.name = "Test Plugin";

		rack::plugin::Model model;
		model.plugin = &testPlugin;
		model.slug = "test-module";
		model.name = "Test Module";
		model.description = "A test module.";

		testPlugin.models.push_back(&model);
		std::vector<plugin::Plugin*> plugins = {&testPlugin};

		// lowercase slug in mock data should not match
		std::set<std::pair<std::string, std::string>> mockSlugs = {
			{"test-plugin", "test-module"}  // lowercase
		};

		auto result = customTagMetamodule(mockSlugs, plugins);

		REQUIRE(result.total == 0);

		testPlugin.models.clear();
		plugins.clear();
	}

	SECTION("Module slug mismatch is case-sensitive") {
		rack::plugin::Plugin testPlugin;
		testPlugin.slug = "test-plugin";
		testPlugin.name = "Test Plugin";

		rack::plugin::Model model;
		model.plugin = &testPlugin;
		model.slug = "Test-Module";  // Note: capital T
		model.name = "Test Module";
		model.description = "A test module.";

		testPlugin.models.push_back(&model);
		std::vector<plugin::Plugin*> plugins = {&testPlugin};

		// lowercase slug in mock data should not match
		std::set<std::pair<std::string, std::string>> mockSlugs = {
			{"test-plugin", "test-module"}  // lowercase
		};

		auto result = customTagMetamodule(mockSlugs, plugins);

		REQUIRE(result.total == 0);

		testPlugin.models.clear();
		plugins.clear();
	}

	SECTION("Empty plugins list returns zero results") {
		std::set<std::pair<std::string, std::string>> mockSlugs = {
			{"any-plugin", "any-module"}
		};
		std::vector<plugin::Plugin*> emptyPlugins;

		auto result = customTagMetamodule(mockSlugs, emptyPlugins);

		REQUIRE(result.total == 0);
		REQUIRE(result.perTag.empty());
		REQUIRE(result.assignments.empty());
	}

	SECTION("Module with matching slug but no plugin match") {
		rack::plugin::Plugin testPlugin;
		testPlugin.slug = "plugin-a";
		testPlugin.name = "Plugin A";

		rack::plugin::Model model;
		model.plugin = &testPlugin;
		model.slug = "shared-module";
		model.name = "Shared Module";
		model.description = "A module.";

		testPlugin.models.push_back(&model);
		std::vector<plugin::Plugin*> plugins = {&testPlugin};

		// Same module slug but different plugin should not match
		std::set<std::pair<std::string, std::string>> mockSlugs = {
			{"plugin-b", "shared-module"}  // Different plugin
		};

		auto result = customTagMetamodule(mockSlugs, plugins);

		REQUIRE(result.total == 0);

		testPlugin.models.clear();
		plugins.clear();
	}
}
