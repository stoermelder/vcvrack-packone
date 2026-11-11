# VCV Library user manual
#
# Generic for VCV Rack plugins: plugin-specific settings are grouped at the top, below this
# comment. Include this file after $(RACK_DIR)/plugin.mk.
#
# The Markdown files in the docs folder (plus README.md and CHANGELOG.md) remain the single
# source for GitHub. `make manual`, which `make dist` runs, preprocesses them into the flat layout
# the VCV Library expects and packages it with the plugin. The manual/ folder is generated and
# git-ignored. Only make, sh, POSIX awk and jq (already required by plugin.mk) are needed, so this
# runs in the build toolchain.
#
#   manual/plugin.md            <- README.md
#   manual/changelog.md         <- CHANGELOG.md
#   manual/<Page>.md            <- <docs>/.../<Page>.md (page names must be unique)
#   manual/<slug>.md            <- copy of the page a module's "manualUrl" in plugin.json points
#                                  to, for modules documented under another name (expanders etc.)
#   manual/images/<image>       <- images linked from the docs (flat, names must be unique),
#                                  only when referenced
#   manual/<path>/<file>        <- other files linked from <docs>/<path>/, only when referenced
#
# Links to external sites (YouTube included) are reduced to their text, and lines holding only
# a URL (YouTube embeds) are dropped, except inside manual:only blocks. Images are kept.
#
# Content can be dropped from (e.g. large animated GIFs) or added to the manual with markers that
# GitHub does not render, each on a line of its own:
#
#   <!-- manual:exclude -->
#   ![Animation](./intro.gif)
#   <!-- /manual:exclude -->
#
#   <!-- manual:only
#   Text shown in the manual only.
#   -->
#
# On `make dist`, plugin.json is rewritten in place: manualUrl/changelogUrl entries pointing into
# the repository (MANUAL_GITHUB) are pointed to the Library manual instead.
#
# See scripts/manual.awk for details.

# --- Plugin-specific settings ---

# URL prefix of repository files, e.g. https://github.com/<user>/<repo>/blob/<branch>/ (required)
MANUAL_GITHUB := https://github.com/stoermelder/vcvrack-packone/blob/v2/
# Folder holding the module manuals
MANUAL_DOCS := docs
# Markdown sources of the manual
MANUAL_SOURCES := $(wildcard README.md CHANGELOG.md) $(sort $(wildcard $(MANUAL_DOCS)/*.md $(MANUAL_DOCS)/*/*.md))
# Linked files with these extensions (separated by |) are not copied, their links become text
MANUAL_SKIP_EXT := syx|vcvss
# Folder of manual.awk and manual.jq
MANUAL_SCRIPTS := scripts

# --- End of settings ---

MANUAL_DIR := manual
MANUAL_LIBRARY := https://library.vcvrack.com/$(SLUG)

DISTRIBUTABLES += $(MANUAL_DIR)

.PHONY: manual manual-urls clean-manual

manual:
	@if [ -z "$(MANUAL_GITHUB)" ]; then \
		echo "error: MANUAL_GITHUB is not set (see plugin-manual.mk)" >&2; \
		exit 1; \
	fi
	@if grep -q 'library\.vcvrack\.com' plugin.json; then \
		echo "error: plugin.json already points to the VCV Library (rewritten by 'make dist'), restore it with 'git checkout plugin.json'" >&2; \
		exit 1; \
	fi
	@rm -rf $(MANUAL_DIR)
	@mkdir -p $(MANUAL_DIR)
	@set -e; \
	dupes=`for f in $(MANUAL_SOURCES); do echo "$${f##*/}"; done | sort | uniq -d`; \
	if [ -n "$$dupes" ]; then echo "error: duplicate manual page names: $$dupes" >&2; exit 1; fi; \
	for f in $(MANUAL_SOURCES); do \
		case "$$f" in \
			README.md) page=plugin.md ;; \
			CHANGELOG.md) page=changelog.md ;; \
			*) page="$${f##*/}" ;; \
		esac; \
		awk -v src="$$f" -v docs="$(MANUAL_DOCS)" -v github="$(MANUAL_GITHUB)" \
			-v assets="$(MANUAL_DIR)/.assets" -v skipext='$(MANUAL_SKIP_EXT)' \
			-f $(MANUAL_SCRIPTS)/manual.awk "$$f" > "$(MANUAL_DIR)/$$page"; \
	done; \
	awk -F'"' -v prefix="$(MANUAL_GITHUB)$(MANUAL_DOCS)/" '$$2 == "slug" { slug = $$4 } \
		$$2 == "manualUrl" && index($$4, prefix) == 1 { page = $$4; sub(/#.*/, "", page); sub(/.*\//, "", page); print slug, page }' \
		plugin.json | while read -r slug page; do \
		if [ ! -f "$(MANUAL_DIR)/$$slug.md" ] && [ -f "$(MANUAL_DIR)/$$page" ]; then \
			cp "$(MANUAL_DIR)/$$page" "$(MANUAL_DIR)/$$slug.md"; \
		fi; \
	done; \
	touch "$(MANUAL_DIR)/.assets"; \
	tab=`printf '\t'`; \
	sort -u "$(MANUAL_DIR)/.assets" > "$(MANUAL_DIR)/.assets.sorted"; \
	dupes=`cut -f1 "$(MANUAL_DIR)/.assets.sorted" | uniq -d`; \
	if [ -n "$$dupes" ]; then echo "error: different files map to the same manual path: $$dupes" >&2; exit 1; fi; \
	while IFS="$$tab" read -r dest asset; do \
		if [ -f "$$asset" ]; then \
			mkdir -p "$(MANUAL_DIR)/`dirname "$$dest"`"; \
			cp "$$asset" "$(MANUAL_DIR)/$$dest"; \
		else \
			echo "warning: manual: linked file $$asset not found" >&2; \
		fi; \
	done < "$(MANUAL_DIR)/.assets.sorted"; \
	rm -f "$(MANUAL_DIR)/.assets.sorted" "$(MANUAL_DIR)/.assets"; \
	echo "manual: `ls $(MANUAL_DIR)/*.md | wc -l | tr -d ' '` pages, `du -sh $(MANUAL_DIR) | cut -f1` in $(MANUAL_DIR)/"

# Points the manualUrls in plugin.json from the repository to the Library manual. This rewrites
# the local plugin.json in place (meant for the build pipeline; locally, `git checkout plugin.json`
# afterwards). It runs after `manual`, which needs the original URLs to create the slug pages.
manual-urls: manual
	jq --tab --arg github "$(MANUAL_GITHUB)" --arg library "$(MANUAL_LIBRARY)" \
		-f $(MANUAL_SCRIPTS)/manual.jq plugin.json > plugin.json.tmp
	mv plugin.json.tmp plugin.json

dist: manual-urls

clean: clean-manual

clean-manual:
	rm -rf $(MANUAL_DIR)
