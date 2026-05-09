#include "Mb_autotag.hpp"

namespace StoermelderPackOne {
namespace Mb {

AutoTagResult customTagAuto(const std::vector<AutoTagRule>& rules, const std::vector<Plugin*>& plugins) {
	// Build a dedicated DB using name + description (always include description
	// regardless of the global searchDescriptions setting).
	fuzzysearch::Database<plugin::Model*> db;
	db.setWeights({0.95f, 1.f});
	db.setThreshold(0.7f);
	for (plugin::Plugin* p : plugins) {
		for (plugin::Model* model : p->models) {
			db.addEntry(model, {model->name, model->description});
		}
	}

	AutoTagResult result;
	for (const AutoTagRule& rule : rules) {
		std::set<plugin::Model*> matches;
		for (const std::string& kw : rule.keywords) {
			bool multiWord = kw.find(' ') != std::string::npos;
			float threshold = multiWord ? rule.minScore : rule.minScore + 0.05f;
			for (const auto& r1 : db.search(kw)) {
				if (r1.score >= threshold) {
					bool blocked = false;
					for (const std::string& bw : rule.blockwords) {
						// Search the fuzzy DB for the blockword. If it matches with high score, it's blocked.
						for (const auto& r2 : db.search(bw)) {
							if (r2.score >= 0.95f && r1.key == r2.key) {
								// Block only if THIS model matches the blockword
								blocked = true;
								break;
							}
						}
						if (blocked) break;
					}
					if (!blocked) {
						matches.insert(r1.key);
					}
				}
			}
		}
		for (plugin::Model* model : matches) {
			if (!customTagHas(model, rule.tagName, true)) {
				result.assignments[rule.tagName].insert(model);
				result.total++;
				result.perTag[rule.tagName]++;
			}
		}
	}
	return result;
}

AutoTagResult customTagSearch(const std::string& query, const std::vector<Plugin*>& plugins) {
	fuzzysearch::Database<plugin::Model*> db;
	db.setWeights({0.9f, 1.f});
	db.setThreshold(0.7f);
	for (plugin::Plugin* p : plugins) {
		for (plugin::Model* model : p->models) {
			db.addEntry(model, {model->name, model->description});
		}
	}

	AutoTagResult result;
	for (const auto& r : db.search(query)) {
		plugin::Model* model = r.key;
		if (!customTagHas(model, query)) {
			result.assignments[query].insert(model);
			result.total++;
			result.perTag[query]++;
		}
	}
	return result;
}


// Performs network download and YAML parsing
const std::string downloadMetamoduleYaml() {
	std::string tmpFile = rack::system::getTempDirectory() + "/metamodule-plugins.yml";

	if (!rack::network::requestDownload("https://metamodule.info/dl/plugins.yml", tmpFile))
		return "";

	return tmpFile;
}

// Parses the downloaded YAML file to extract plugin-module slug mappings.
std::set<std::pair<std::string, std::string>> parseMetamoduleYaml(const std::string& tmpFile) {
	std::set<std::pair<std::string, std::string>> result;

	FILE* file = fopen(tmpFile.c_str(), "r");
	if (!file) return result;

	const std::string prefix = "VCVSlug: ";
	std::string currentPlugin;
	char buf[512];

	while (fgets(buf, sizeof(buf), file)) {
		std::string line(buf);
		// Count leading spaces to determine nesting level
		size_t indent = 0;
		while (indent < line.size() && line[indent] == ' ') indent++;
		std::string trimmed = line.substr(indent);
		// Strip trailing whitespace/newline
		while (!trimmed.empty() && (trimmed.back() == '\n' || trimmed.back() == '\r' || trimmed.back() == ' '))
			trimmed.pop_back();

		if (trimmed.find(prefix) != 0) continue;
		std::string slug = trimmed.substr(prefix.size());

		if (indent == 8) {
			currentPlugin = slug;
		} 
		else if (indent == 16 && !currentPlugin.empty()) {
			result.insert({currentPlugin, slug});
		}
	}

	fclose(file);
	return result;
}

AutoTagResult customTagMetamodule(std::set<std::pair<std::string, std::string>> metamoduleModules, const std::vector<Plugin*>& plugins) {
	AutoTagResult result;
	for (plugin::Plugin* p : plugins) {
		for (plugin::Model* model : p->models) {
			if (metamoduleModules.count({p->slug, model->slug}) && !customTagHas(model, "MetaModule", true)) {
				result.assignments["MetaModule"].insert(model);
				result.total++;
				result.perTag["MetaModule"]++;
			}
		}
	}
	return result;
}

} // namespace Mb
} // namespace StoermelderPackOne