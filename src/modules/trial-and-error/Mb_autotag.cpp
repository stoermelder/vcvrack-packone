#include "Mb_autotag.hpp"
#include "../../vcv/api.hpp"
#include <sstream>
#include <cstring>

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
	std::string tmpFile = vcv::fs::getTempDirectory() + "/metamodule-plugins.yml";

	if (!vcv::nw::requestDownload("https://metamodule.info/dl/plugins.yml", tmpFile))
		return "";

	return tmpFile;
}

// Parses the downloaded YAML file to extract plugin-module slug mappings.
std::set<std::pair<std::string, std::string>> parseMetamoduleYaml(const std::string& tmpFile) {
	std::set<std::pair<std::string, std::string>> result;

	std::string data;
	if (!vcv::fs::read(tmpFile, data)) return result;

	const std::string prefix = "VCVSlug: ";
	std::string currentPlugin;
	std::istringstream stream(data);
	std::string line;

	while (std::getline(stream, line)) {
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

// Known open source licenses (OSI approved or FSF free), SPDX ids lowercased with the
// "-only"/"-or-later"/"+" suffixes of the GNU family removed.
static const std::set<std::string>& freeLicenseIds() {
	static const std::set<std::string> ids = {
		"gpl-2.0", "gpl-3.0", "lgpl-2.0", "lgpl-2.1", "lgpl-3.0", "agpl-3.0",
		"mit", "mit-0", "x11", "isc", "zlib", "unlicense", "cc0-1.0", "wtfpl", "bsl-1.0", "0bsd",
		"apache-1.1", "apache-2.0",
		"bsd-2-clause", "bsd-3-clause", "bsd-3-clause-clear", "bsd-4-clause", "bsd-2-clause-patent",
		"mpl-1.1", "mpl-2.0", "epl-1.0", "epl-2.0", "eupl-1.1", "eupl-1.2", "osl-3.0", "artistic-2.0",
		"cc-by-3.0", "cc-by-4.0", "cc-by-sa-3.0", "cc-by-sa-4.0",
		"ofl-1.1", "ncsa", "postgresql", "python-2.0", "libpng-2.0", "bsd-1-clause", "afl-3.0",
		"ecl-2.0", "cecill-2.1", "gpl-2.0-with-classpath-exception", "ms-pl", "ms-rl", "ipl-1.0", "cpl-1.0",
	};
	return ids;
}

static bool licenseIdIsFree(std::string id) {
	id = string::lowercase(string::trim(id));
	while (!id.empty() && (id.front() == '(' || id.front() == ')')) id.erase(0, 1);
	while (!id.empty() && (id.back() == '(' || id.back() == ')')) id.pop_back();
	// "GPL-3.0-or-later WITH exception": the exception does not change the license family
	size_t with = id.find(" with ");
	if (with != std::string::npos) id = id.substr(0, with);
	if (id.empty()) return false;
	if (id.back() == '+') id.pop_back();
	for (const char* suffix : {"-or-later", "-only"}) {
		size_t n = std::strlen(suffix);
		if (id.size() > n && id.compare(id.size() - n, n, suffix) == 0) {
			id.erase(id.size() - n);
			break;
		}
	}
	return freeLicenseIds().count(id) > 0;
}

// Splits `s` at every case-insensitive occurrence of the whole word `op` (e.g. " or ").
static std::vector<std::string> licenseSplit(const std::string& s, const std::string& op) {
	std::vector<std::string> parts;
	std::string lower = string::lowercase(s);
	size_t start = 0, pos;
	while ((pos = lower.find(op, start)) != std::string::npos) {
		parts.push_back(s.substr(start, pos - start));
		start = pos + op.size();
	}
	parts.push_back(s.substr(start));
	return parts;
}

bool licenseIsFree(const std::string& license) {
	// Operator precedence in SPDX: AND binds tighter than OR; parentheses are not nested in practice
	for (const std::string& orPart : licenseSplit(license, " or ")) {
		bool all = true;
		for (const std::string& andPart : licenseSplit(orPart, " and ")) {
			if (!licenseIdIsFree(andPart)) { all = false; break; }
		}
		if (all) return true;
	}
	return false;
}

bool licenseKeyExists(const std::string& pluginSlug) {
	if (pluginSlug.empty()) return false;
	return vcv::fs::exists(vcv::fs::getUserDirectory("licenses/" + pluginSlug + ".vcvkey"));
}

AutoTagResult customTagLicense(const std::vector<Plugin*>& plugins, std::function<bool(const std::string&)> hasLicenseKey) {
	AutoTagResult result;
	for (plugin::Plugin* p : plugins) {
		const std::string tag = (licenseIsFree(p->license) || !hasLicenseKey(p->slug)) ? "Free" : "Commercial";
		for (plugin::Model* model : p->models) {
			if (customTagHas(model, tag, true)) continue;
			result.assignments[tag].insert(model);
			result.total++;
			result.perTag[tag]++;
		}
	}
	return result;
}

} // namespace Mb
} // namespace StoermelderPackOne