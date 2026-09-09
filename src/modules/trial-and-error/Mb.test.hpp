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