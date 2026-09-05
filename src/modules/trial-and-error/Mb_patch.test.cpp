#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"
#include "../../test/test_mock.hpp"
#include "Mb_patch_source_filesystem.hpp"
#include "Mb_patch_source_patchstorage.hpp"
#include "Mb_patch_preview.hpp"

using namespace StoermelderPackOne;
using namespace StoermelderPackOne::Mb;
using namespace StoermelderPackOne::Mb::patch;

SYNC_MODEL(modelMb, "Mb");
Test::TestContext<> testContext;

// The MB selection browser used to talk to fopen/osdialog directly. These tests
// install recording mocks to prove the migrated calls route through the
// swappable vcv layer instead.

// Map-backed FileAccess: missing keys read as "cannot open". Path helpers
// forward to the real rack::system (pure string manipulation).
struct MockFileAccess : vcv::FileAccess {
	struct ReadCall { std::string path; };
	struct WriteCall { std::string path, data; };
	mutable std::vector<ReadCall> reads;
	std::vector<WriteCall> writes;
	std::map<std::string, std::string> files;
	bool writeResult = true;

	bool read(const std::string& path, std::string& data) const override {
		reads.push_back({path});
		auto it = files.find(path);
		if (it == files.end()) return false;
		data = it->second;
		return true;
	}
	bool write(const std::string& path, const std::string& data) override {
		writes.push_back({path, data});
		if (!writeResult) return false;
		files[path] = data;
		return true;
	}
	bool exists(const std::string& path) const override {
		if (files.find(path) != files.end()) return true;
		return rack::system::exists(path);
	}
	std::string join(const std::string& a, const std::string& b) override {
		return rack::system::join(a, b);
	}
	std::string getDirectory(const std::string& path) override {
		return rack::system::getDirectory(path);
	}
	std::string getFilename(const std::string& path) override {
		return rack::system::getFilename(path);
	}
	std::vector<std::string> getEntries(const std::string& dir, int depth) override {
		return rack::system::getEntries(dir, depth);
	}
	bool isFile(const std::string& path) override {
		if (files.find(path) != files.end()) return true;
		return rack::system::isFile(path);
	}
	bool isDirectory(const std::string& path) override {
		return rack::system::isDirectory(path);
	}
	bool remove(const std::string& path) override {
		files.erase(path);
		return true;
	}
	int removeRecursively(const std::string& path) override {
		files.erase(path);
		return 0;
	}
};

// UiAccess mock with scripted folder/save answers and a browser log.
struct MockUiAccess : vcv::UiAccess {
	struct SaveCall { std::string filters, dir, filename; };
	std::vector<std::string> dirResults;
	std::vector<SaveCall> saveCalls;
	std::vector<std::string> saveResults;
	std::vector<std::string> openedBrowsers;
	int dirIndex = 0;
	int saveIndex = 0;

	std::string openDirDialog() override {
		if (dirIndex < (int) dirResults.size()) return dirResults[dirIndex++];
		return "";
	}
	std::string saveDialog(const std::string& filters, const std::string& dir, const std::string& filename) override {
		saveCalls.push_back({filters, dir, filename});
		if (saveIndex < (int) saveResults.size()) return saveResults[saveIndex++];
		return "";
	}
	void openBrowser(const std::string& url) override {
		openedBrowsers.push_back(url);
	}
};

// NwAccess mock with a scripted JSON answer.
struct MockNwAccess : vcv::NwAccess {
	struct DownloadCall { std::string url, filename; };
	std::vector<DownloadCall> downloads;
	bool downloadResult = true;
	json_t* jsonResult = nullptr; // borrowed; test owns the original

	json_t* requestJson(vcv::Method, const std::string&, json_t*, const std::map<std::string, std::string>&) override {
		return jsonResult ? json_incref(jsonResult), jsonResult : nullptr;
	}
	bool requestDownload(const std::string& url, const std::string& filename, float*, const std::map<std::string, std::string>&) override {
		downloads.push_back({url, filename});
		return downloadResult;
	}
};

static filesystem::FileSystemSource makeVcvsSource(const std::string& root) {
	filesystem::FileSystemSource src;
	src.slug = filesystem::FileSystemSource::SLUG_VCVS;
	src.rootContainer = root;
	src.currentContainer = "/";
	src.index = std::make_shared<filesystem::FileSystemPatchSourceIndex>();
	src.archiveCache = std::make_shared<std::map<std::string, filesystem::FileSystemSource::ArchiveCacheEntry>>();
	return src;
}

TEST_CASE("isVcvLegacyV1 reads the magic through the fs layer", "[Mb][patch][fs]") {
	auto mock = Test::makeMockVcv<MockFileAccess>();
	mock.fs.files["/z.vcv"] = std::string("\x28\xb5\x2f\xfd", 4) + "rest";
	mock.fs.files["/plain.vcv"] = "{\"modules\": []}";

	CHECK(PatchSource::isVcvLegacyV1("/z.vcv") == false);
	CHECK(PatchSource::isVcvLegacyV1("/plain.vcv") == true);
	CHECK(PatchSource::isVcvLegacyV1("/missing.vcv") == false);
	REQUIRE(mock.fs.reads.size() == 3);
}

TEST_CASE("FileSystemSource getFileJson reads .vcvs through the fs layer", "[Mb][patch][fs]") {
	auto mock = Test::makeMockVcv<MockFileAccess>();
	auto src = makeVcvsSource("/root");
	mock.fs.files["/root/a.vcvs"] = "{\"modules\": [], \"cables\": []}";

	SECTION("Valid file parses") {
		json_t* rootJ = src.getFileJson("/a.vcvs");
		REQUIRE(rootJ != nullptr);
		CHECK(json_object_get(rootJ, "modules") != nullptr);
		json_decref(rootJ);
		REQUIRE(mock.fs.reads.size() == 1);
		CHECK(mock.fs.reads[0].path == "/root/a.vcvs");
	}

	SECTION("Missing file returns null") {
		CHECK(src.getFileJson("/missing.vcvs") == nullptr);
	}
}

TEST_CASE("FileSystemSource selectFolder routes through the UI layer", "[Mb][patch][ui]") {
	auto mock = Test::makeMockVcv<MockUiAccess>();

	SECTION("Cancelled dialog returns empty") {
		CHECK(filesystem::FileSystemSource::selectFolder() == "");
	}

	SECTION("Chosen folder is returned") {
		mock.ui.dirResults.push_back("/my/patches");
		CHECK(filesystem::FileSystemSource::selectFolder() == "/my/patches");
	}
}

TEST_CASE("FileSystemSource convertVcvssFile routes through ui + fs", "[Mb][patch][ui][fs]") {
	auto mock = Test::makeMockVcv<MockUiAccess, MockFileAccess>();
	auto src = makeVcvsSource("/root");
	// Unknown models get width 0 from the production lookup, so positions stay 0.
	mock.fs.files["/root/strip.vcvss"] = "{\"leftModules\": [], \"rightModules\": [], \"cables\": []}";

	SECTION("Cancelled save dialog does nothing") {
		src.convertVcvssFile("/strip.vcvss");
		REQUIRE(mock.ui.saveCalls.size() == 1);
		CHECK(mock.ui.saveCalls[0].filters == std::string(vcv::SELECTION_FILTERS));
		CHECK(mock.fs.writes.empty());
	}

	SECTION("Successful convert writes the new file") {
		mock.ui.saveResults.push_back("/root/strip.vcvs");
		src.convertVcvssFile("/strip.vcvss");
		REQUIRE(mock.ui.saveCalls.size() == 1);
		REQUIRE(mock.fs.writes.size() == 1);
		CHECK(mock.fs.writes[0].path == "/root/strip.vcvs");
		CHECK(mock.fs.writes[0].data.find("\"modules\"") != std::string::npos);
	}

	SECTION("Missing source reports without writing") {
		mock.ui.saveResults.push_back("/root/out.vcvs");
		src.convertVcvssFile("/missing.vcvss");
		REQUIRE(mock.ui.saveCalls.size() == 1);
		CHECK(mock.fs.writes.empty());
	}
}

TEST_CASE("FileSystemSource index round-trips through the fs layer", "[Mb][patch][fs]") {
	auto mock = Test::makeMockVcv<MockFileAccess>();
	auto src = makeVcvsSource("/root");
	src.index->setDescription("/a.vcvs", "hello");
	src.index->setFavorite("/a.vcvs", true);

	src.saveIndex();
	REQUIRE(mock.fs.writes.size() == 1);
	CHECK(mock.fs.writes[0].path == "/root/mb-index.vcvs.json");

	// Load it back into a fresh source object.
	auto src2 = makeVcvsSource("/root");
	src2.loadIndex();
	CHECK(src2.index->getDescription("/a.vcvs") == "hello");
	CHECK(src2.index->isFavorite("/a.vcvs") == true);
}

TEST_CASE("PatchStorageSource saveToDisk routes through ui + fs", "[Mb][patch][ui][fs]") {
	auto mock = Test::makeMockVcv<MockUiAccess, MockFileAccess>();
	patchstorage::PatchStorageSource src;
	src.patches = std::make_shared<std::map<std::string, std::string>>();
	src.patchInfo = std::make_shared<std::map<std::string, patchstorage::PatchInfo>>();
	patchstorage::PatchInfo info;
	info.slug = "my-patch";
	info.filename = "my-patch.vcv";
	(*src.patchInfo)["42"] = info;
	mock.fs.files["/cache/my-patch.vcv"] = "PATCHDATA";
	(*src.patches)["42"] = "/cache/my-patch.vcv";

	SECTION("Cancelled save dialog writes nothing") {
		src.saveToDisk("42", info);
		REQUIRE(mock.ui.saveCalls.size() == 1);
		CHECK(mock.ui.saveCalls[0].filename == "my-patch.vcv");
		CHECK(mock.fs.writes.empty());
	}

	SECTION("Chosen path copies the download") {
		mock.ui.saveResults.push_back("/out/my-patch.vcv");
		src.saveToDisk("42", info);
		REQUIRE(mock.ui.saveCalls.size() == 1);
		REQUIRE(mock.fs.writes.size() == 1);
		CHECK(mock.fs.writes[0].path == "/out/my-patch.vcv");
		CHECK(mock.fs.writes[0].data == "PATCHDATA");
	}
}

TEST_CASE("PatchStorageSource fetchJson routes through the network layer", "[Mb][patch][nw]") {
	auto mock = Test::makeMockVcv<MockNwAccess>();
	patchstorage::PatchStorageSource src;
	json_t* answer = json_object();
	json_object_set_new(answer, "ok", json_true());
	mock.nw.jsonResult = answer;

	json_t* got = src.fetchJson("https://example.com/x");
	REQUIRE(got != nullptr);
	CHECK(json_is_true(json_object_get(got, "ok")));
	json_decref(got);
	json_decref(answer);
	mock.nw.jsonResult = nullptr;
}

TEST_CASE("PreviewWidget records missing models without crashing", "[Mb][patch][preview]") {
	// Unknown plugin/model slugs exercise the missingModels path; no widget
	// is created for them, so this stays headless-safe.
	PreviewWidget preview;
	std::string data = "{\"modules\": ["
		"{\"plugin\": \"NoSuchPlugin\", \"model\": \"Missing\", \"id\": 7, \"pos\": [1, 2]},"
		"{\"plugin\": \"NoSuchPlugin\", \"id\": 8, \"pos\": [0, 0]}"
		"], \"cables\": []}";
	std::string err;
	json_t* rootJ = vcv::parseJson(data, err);
	REQUIRE(rootJ != nullptr);

	REQUIRE(preview.setPatch("test", rootJ) == true); // takes ownership
	const auto& missing = preview.getMissingModels();
	REQUIRE(missing.size() == 1);
	CHECK(missing.begin()->second == "NoSuchPlugin/Missing");
	// Malformed entry (no model slug) is skipped, not recorded.
}
