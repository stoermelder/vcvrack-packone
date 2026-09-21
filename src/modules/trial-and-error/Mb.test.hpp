#pragma once

// Shared preamble for the MB test suite.
// Included by Mb.test.cpp, which pulls the test cases in from Mb.test.module.hpp
// inside their own namespace.

#include "../../test/framework.hpp"
#include "Mb.cpp"
#include "Mb.hpp"

using namespace StoermelderPackOne;
using namespace StoermelderPackOne::Mb;

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

// parseMetamoduleYaml reads the YAML through vcv::fs::read, and
// openAutoTagConfirmDialog surfaces the "no assignments" case through
// vcv::ui::message.
//
// createDirectory/remove/rename/copy default to true (unlike the base class's false), since
// most callers only care that the sequence runs, not that any one step is scripted to fail —
// matching manifestsCacheDownload's own "succeed unless told otherwise" happy path. Set the
// corresponding *Result field to false to exercise a specific failure branch.
struct MockFileAccess : vcv::FileAccess {
	struct ReadCall { std::string path; };
	mutable std::vector<ReadCall> reads;
	std::map<std::string, std::string> files;  // path → contents; missing = cannot open

	bool read(const std::string& path, std::string& data) const override {
		reads.push_back({path});
		auto it = files.find(path);
		if (it == files.end()) return false;
		data = it->second;
		return true;
	}

	std::vector<std::string> createDirectoryCalls;
	bool createDirectoryResult = true;
	bool createDirectory(const std::string& path) override {
		createDirectoryCalls.push_back(path);
		return createDirectoryResult;
	}

	std::vector<std::string> removeCalls;
	bool removeResult = true;
	bool remove(const std::string& path) override {
		removeCalls.push_back(path);
		return removeResult;
	}

	struct RenameCall { std::string srcPath, destPath; };
	std::vector<RenameCall> renameCalls;
	bool renameResult = true;
	bool rename(const std::string& srcPath, const std::string& destPath) override {
		renameCalls.push_back({srcPath, destPath});
		return renameResult;
	}

	struct CopyCall { std::string srcPath, destPath; };
	std::vector<CopyCall> copyCalls;
	bool copyResult = true;
	bool copy(const std::string& srcPath, const std::string& destPath) override {
		copyCalls.push_back({srcPath, destPath});
		return copyResult;
	}
};

// A UiAccess mock that records message() calls.
struct MockUiAccess : vcv::UiAccess {
	struct Message { vcv::MessageType type; vcv::MessageButtons buttons; std::string msg; };
	std::vector<Message> messages;

	bool message(vcv::MessageType type, vcv::MessageButtons buttons, const std::string& msg) override {
		messages.push_back({type, buttons, msg});
		return true;
	}
};

// A HistoryAccess mock that records pushed actions instead of calling APP->history->push(),
// which TestContext never initializes (ctx->history stays null). Needed for chooseModel()
// (Mb.cpp), migrated to the vcv::history seam so a real click-to-add module doesn't segfault
// headless. Takes ownership like the real Rack history::State does, so a test that installs
// this and never inspects `pushed` still doesn't leak.
struct MockHistoryAccess : vcv::HistoryAccess {
	std::vector<rack::history::Action*> pushed;

	void push(rack::history::Action* a) override {
		pushed.push_back(a);
	}

	~MockHistoryAccess() {
		for (auto* a : pushed) delete a;
	}
};

// A NwAccess mock that records requestDownload() calls and returns scripted answers.
struct MockNwAccess : vcv::NwAccess {
	struct DownloadCall { std::string url, filename; };
	std::vector<DownloadCall> downloads;
	bool downloadResult = true;  // default: success

	bool requestDownload(const std::string& url, const std::string& filename, float* progress,
	                     const std::map<std::string, std::string>& cookies) override {
		downloads.push_back({url, filename});
		return downloadResult;
	}
};

// Suite-wide guard: BrowserOverlay's ctor/dtor (Mb.cpp) does real vcv::fs::* I/O
// (mb-widths.json, manifests cache, pluginSettings.saveToJson()), and most TEST_CASEs never
// install their own FileAccess mock. Installing MockFileAccess here for the whole binary
// stops that from writing into the developer's real Rack user directory. A TEST_CASE that
// needs specific file contents still installs its own TEST_MOCK_FS(MockFileAccess) —
// Test::mock::Guard is LIFO, so it shadows this one for its scope.
//
// Doesn't cover manifestsCacheInit()'s detached thread, which reads vcv::fileAccess
// unsynchronized — a separate, pre-existing race, harmless today only because
// mbNewestAutoUpdate defaults to false.
MockFileAccess suiteFileAccess;
Test::mock::Guard<vcv::FileAccess> suiteFileAccessGuard{vcv::fileAccess, &suiteFileAccess};