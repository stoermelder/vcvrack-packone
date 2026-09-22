// MB manifests test cases. Included by Mb.test.cpp inside namespace __manifests.
// Not a standalone header: Mb.test.hpp supplies everything these cases use.


// Helper to build a mock plugin with the given models (slug-only, sufficient for lookup).
// plugin::Plugin::models is a std::list, so the created Model*s are returned separately
// (in insertion order) for index-based access in tests.
static plugin::Plugin* createMockPlugin(const std::string& pluginSlug, const std::vector<std::string>& modelSlugs, std::vector<plugin::Model*>* outModels = nullptr) {
	plugin::Plugin* p = new plugin::Plugin;
	p->slug = pluginSlug;
	p->name = pluginSlug;
	for (const std::string& modelSlug : modelSlugs) {
		plugin::Model* m = new plugin::Model;
		m->slug = modelSlug;
		m->name = modelSlug;
		m->plugin = p;
		p->models.push_back(m);
		if (outModels) outModels->push_back(m);
	}
	return p;
}

TEST_CASE("findModel", "[Mb][manifests]") {
	plugin::Plugin* p1 = createMockPlugin("plugin-a", {"model-1", "model-2"});
	plugin::Plugin* p2 = createMockPlugin("plugin-b", {"model-1"});
	std::vector<plugin::Plugin*> plugins = {p1, p2};

	SECTION("Finds a model by plugin+model slug") {
		Model* m = findModel(plugins, "plugin-a", "model-2");
		REQUIRE(m != nullptr);
		REQUIRE(m->slug == "model-2");
		REQUIRE(m->plugin == p1);
	}

	SECTION("Distinguishes models with the same slug in different plugins") {
		Model* m = findModel(plugins, "plugin-b", "model-1");
		REQUIRE(m != nullptr);
		REQUIRE(m->plugin == p2);
	}

	SECTION("Returns nullptr for unknown plugin slug") {
		REQUIRE(findModel(plugins, "plugin-x", "model-1") == nullptr);
	}

	SECTION("Returns nullptr for unknown model slug within a known plugin") {
		REQUIRE(findModel(plugins, "plugin-a", "model-x") == nullptr);
	}

	delete p1;
	delete p2;
}


TEST_CASE("manifestsCacheParseJson", "[Mb][manifests]") {
	std::vector<plugin::Model*> pModels;
	plugin::Plugin* p = createMockPlugin("test-plugin", {"model-with-ts", "model-without-ts", "model-not-in-cache"}, &pModels);
	std::vector<plugin::Plugin*> plugins = {p};

	SECTION("Uses the module's own creationTimestamp when present") {
		json_error_t error;
		json_t* rootJ = json_loads(R"({
			"test-plugin": {
				"creationTimestamp": 1000.0,
				"modules": {
					"model-with-ts": { "creationTimestamp": 2000.0 }
				}
			}
		})", 0, &error);
		REQUIRE(rootJ != nullptr);

		auto parsed = manifestsCacheParseJson(rootJ, plugins);
		json_decref(rootJ);

		REQUIRE(parsed.size() == 1);
		REQUIRE(parsed.at(pModels[0]) == 2000);
	}

	SECTION("Falls back to the plugin's creationTimestamp when the module has none") {
		json_error_t error;
		json_t* rootJ = json_loads(R"({
			"test-plugin": {
				"creationTimestamp": 1000.0,
				"modules": {
					"model-without-ts": {}
				}
			}
		})", 0, &error);
		REQUIRE(rootJ != nullptr);

		auto parsed = manifestsCacheParseJson(rootJ, plugins);
		json_decref(rootJ);

		REQUIRE(parsed.size() == 1);
		REQUIRE(parsed.at(pModels[1]) == 1000);
	}

	SECTION("Skips modules that don't resolve against the given plugin list") {
		json_error_t error;
		json_t* rootJ = json_loads(R"({
			"test-plugin": {
				"creationTimestamp": 1000.0,
				"modules": {
					"model-unknown-slug": { "creationTimestamp": 2000.0 }
				}
			}
		})", 0, &error);
		REQUIRE(rootJ != nullptr);

		auto parsed = manifestsCacheParseJson(rootJ, plugins);
		json_decref(rootJ);

		REQUIRE(parsed.empty());
	}

	SECTION("Skips plugins with no 'modules' key entirely") {
		json_error_t error;
		json_t* rootJ = json_loads(R"({
			"test-plugin": {
				"creationTimestamp": 1000.0
			}
		})", 0, &error);
		REQUIRE(rootJ != nullptr);

		auto parsed = manifestsCacheParseJson(rootJ, plugins);
		json_decref(rootJ);

		REQUIRE(parsed.empty());
	}

	SECTION("Parses multiple modules across multiple plugins") {
		std::vector<plugin::Model*> p2Models;
		plugin::Plugin* p2 = createMockPlugin("other-plugin", {"other-model"}, &p2Models);
		std::vector<plugin::Plugin*> multiPlugins = {p, p2};

		json_error_t error;
		json_t* rootJ = json_loads(R"({
			"test-plugin": {
				"creationTimestamp": 1000.0,
				"modules": {
					"model-with-ts": { "creationTimestamp": 2000.0 },
					"model-without-ts": {}
				}
			},
			"other-plugin": {
				"creationTimestamp": 3000.0,
				"modules": {
					"other-model": { "creationTimestamp": 4000.0 }
				}
			}
		})", 0, &error);
		REQUIRE(rootJ != nullptr);

		auto parsed = manifestsCacheParseJson(rootJ, multiPlugins);
		json_decref(rootJ);

		REQUIRE(parsed.size() == 3);
		REQUIRE(parsed.at(pModels[0]) == 2000);
		REQUIRE(parsed.at(pModels[1]) == 1000);
		REQUIRE(parsed.at(p2Models[0]) == 4000);

		delete p2;
	}

	delete p;
}


TEST_CASE("manifestCreationTimestampGet", "[Mb][manifests]") {
	std::vector<plugin::Model*> pModels;
	plugin::Plugin* p = createMockPlugin("test-plugin", {"known-model"}, &pModels);
	std::vector<plugin::Plugin*> plugins = {p};

	json_error_t error;
	json_t* rootJ = json_loads(R"({
		"test-plugin": {
			"creationTimestamp": 1000.0,
			"modules": {
				"known-model": { "creationTimestamp": 5000.0 }
			}
		}
	})", 0, &error);
	REQUIRE(rootJ != nullptr);

	{
		std::lock_guard<std::mutex> lock(manifestsMutex);
		manifestCreationTimestamps = manifestsCacheParseJson(rootJ, plugins);
	}
	json_decref(rootJ);

	SECTION("Returns the parsed timestamp for a known model") {
		REQUIRE(manifestCreationTimestampGet(pModels[0]) == 5000);
	}

	SECTION("Returns -1 for a model absent from the cache") {
		plugin::Model unknown;
		REQUIRE(manifestCreationTimestampGet(&unknown) == -1);
	}

	// Reset shared state so other test cases in this binary aren't affected.
	{
		std::lock_guard<std::mutex> lock(manifestsMutex);
		manifestCreationTimestamps.clear();
	}
	delete p;
}


TEST_CASE("manifestsCacheIsStale", "[Mb][manifests]") {
	std::string cacheFile = rack::system::getTempDirectory() + "/mb-manifests-cache-test.json";
	std::string pluginDir = rack::system::getTempDirectory() + "/mb-manifests-test-plugin";
	rack::system::createDirectory(pluginDir);
	std::string pluginManifest = rack::system::join(pluginDir, "plugin.json");

	auto touch = [](const std::string& path) {
		FILE* f = fopen(path.c_str(), "w");
		REQUIRE(f != nullptr);
		fputs("{}", f);
		fclose(f);
	};

	SECTION("True when the cache file doesn't exist") {
		std::remove(cacheFile.c_str());
		std::vector<plugin::Plugin*> plugins;
		REQUIRE(manifestsCacheIsStale(cacheFile, plugins) == true);
	}

	SECTION("False when the cache is newer than every plugin manifest") {
		touch(pluginManifest);
		std::this_thread::sleep_for(std::chrono::milliseconds(1100));
		touch(cacheFile);

		plugin::Plugin p;
		p.path = pluginDir;
		std::vector<plugin::Plugin*> plugins = {&p};

		REQUIRE(manifestsCacheIsStale(cacheFile, plugins) == false);
	}

	SECTION("True when a plugin manifest is newer than the cache") {
		touch(cacheFile);
		std::this_thread::sleep_for(std::chrono::milliseconds(1100));
		touch(pluginManifest);

		plugin::Plugin p;
		p.path = pluginDir;
		std::vector<plugin::Plugin*> plugins = {&p};

		REQUIRE(manifestsCacheIsStale(cacheFile, plugins) == true);
	}

	SECTION("Plugins with an empty path (Core) are skipped") {
		touch(cacheFile);

		plugin::Plugin core;
		core.path = "";
		std::vector<plugin::Plugin*> plugins = {&core};

		REQUIRE(manifestsCacheIsStale(cacheFile, plugins) == false);
	}

	std::remove(cacheFile.c_str());
	std::remove(pluginManifest.c_str());
	rack::system::remove(pluginDir);
}


// manifestsCacheFromJson() reads mbManifestsCacheFilePath() (vcv::fs::getUserDirectory(...))
// through vcv::fs::read. FileAccess::getUserDirectory()'s base default ignores its argument
// and always returns "" — so under MockFileAccess (which doesn't override it), that path is
// exactly "", not a real filesystem location. Keying mock.fs.files on "" is what actually
// exercises the read, matching the substring-tolerant approach used elsewhere in this suite
// for the same reason (see downloadMetamoduleYaml's test, Mb.test.autotag.hpp).
TEST_CASE("manifestsCacheFromJson", "[Mb][manifests]") {
	struct Mock {
		TEST_MOCK_FS(MockFileAccess);
	} mock;

	std::vector<plugin::Model*> pModels;
	plugin::Plugin* p = createMockPlugin("test-plugin", {"known-model"}, &pModels);
	std::vector<plugin::Plugin*>& plugins = rack::plugin::plugins;
	plugins.push_back(p);

	// Reset shared state before and after: manifestsCacheFromJson() writes the same globals
	// manifestCreationTimestampGet's test above does, and this test's plugin is only valid for
	// its own duration.
	auto reset = [&]() {
		std::lock_guard<std::mutex> lock(manifestsMutex);
		manifestCreationTimestamps.clear();
	};
	reset();

	SECTION("Loads and parses a valid cache file") {
		mock.fs.files[""] = R"({
			"test-plugin": {
				"creationTimestamp": 1000.0,
				"modules": {
					"known-model": { "creationTimestamp": 5000.0 }
				}
			}
		})";

		manifestsCacheFromJson();

		REQUIRE(manifestsCacheExists() == true);
		REQUIRE(manifestCreationTimestampGet(pModels[0]) == 5000);
	}

	SECTION("Missing cache file leaves the cache unloaded") {
		// No entry in mock.fs.files → read() returns false.
		manifestsCacheFromJson();

		REQUIRE(manifestsCacheExists() == false);
		REQUIRE(manifestCreationTimestampGet(pModels[0]) == -1);
	}

	SECTION("Invalid JSON leaves the cache unloaded") {
		mock.fs.files[""] = "{ not valid json";

		manifestsCacheFromJson();

		REQUIRE(manifestsCacheExists() == false);
	}

	reset();
	plugins.pop_back();
	delete p;
}


// manifestsCacheDownload() downloads via vcv::nw::requestDownload, then replaces the local
// cache file with the download via vcv::fs (createDirectory/remove/rename, falling back to
// copy+remove if rename() fails — e.g. EXDEV across filesystems).
TEST_CASE("manifestsCacheDownload", "[Mb][manifests][nw]") {
	struct Mock {
		TEST_MOCK_FS(MockFileAccess);
		TEST_MOCK_NW(MockNwAccess);
	} mock;

	SECTION("Download failure short-circuits before touching the filesystem") {
		mock.nw.downloadResult = false;

		bool result = manifestsCacheDownload();

		REQUIRE(result == false);
		REQUIRE(mock.nw.downloads.size() == 1);
		CHECK(mock.nw.downloads[0].url == "https://raw.githubusercontent.com/VCVRack/library/v2/manifests-cache.json");
		CHECK(mock.fs.createDirectoryCalls.empty());
		CHECK(mock.fs.renameCalls.empty());
	}

	SECTION("Successful download and rename") {
		bool result = manifestsCacheDownload();

		REQUIRE(result == true);
		CHECK(mock.fs.createDirectoryCalls.size() == 1);
		CHECK(mock.fs.removeCalls.size() == 1);
		REQUIRE(mock.fs.renameCalls.size() == 1);
		CHECK(mock.fs.copyCalls.empty());
	}

	SECTION("rename() failing falls back to copy()") {
		mock.fs.renameResult = false;

		bool result = manifestsCacheDownload();

		REQUIRE(result == true);
		REQUIRE(mock.fs.renameCalls.size() == 1);
		REQUIRE(mock.fs.copyCalls.size() == 1);
		// The temp file is removed after a successful fallback copy.
		CHECK(mock.fs.removeCalls.size() == 2);
	}

	SECTION("Both rename() and copy() failing reports failure") {
		mock.fs.renameResult = false;
		mock.fs.copyResult = false;

		bool result = manifestsCacheDownload();

		REQUIRE(result == false);
		REQUIRE(mock.fs.renameCalls.size() == 1);
		REQUIRE(mock.fs.copyCalls.size() == 1);
		// No fallback removal of the temp file once copy() itself failed.
		CHECK(mock.fs.removeCalls.size() == 1);
	}
}


// manifestsCacheAutoUpdateCheck() is a thin wrapper: download only runs when the cache is
// stale. Uses a nonexistent cache path so manifestsCacheIsStale() reports stale without
// needing real files (see its own TEST_CASE above for that function's direct coverage).
TEST_CASE("manifestsCacheAutoUpdateCheck", "[Mb][manifests][nw]") {
	struct Mock {
		TEST_MOCK_FS(MockFileAccess);
		TEST_MOCK_NW(MockNwAccess);
	} mock;

	SECTION("Downloads when the cache is stale") {
		manifestsCacheAutoUpdateCheck();

		REQUIRE(mock.nw.downloads.size() == 1);
	}
}