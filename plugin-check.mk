# Static analysis targets.
#
#   make cppcheck                                   whole tree, no build needed
#   make tidy FILE=src/modules/dirt/Dirt.cpp        clang-tidy one file
#
# Both print findings and exit 0. Pass STRICT=1 to exit non-zero instead, which is
# what CI should use once the baseline is clean.
#
# Rack sets no policy here (its Building page documents no analysis step), so these
# follow Rack's compiler conventions instead: the same include flags, and the same
# C++ standard the code is actually compiled with.
#
# The plugin builds as C++11 (Rack's compile.mk sets -std=c++11 and nothing here
# overrides it); only the test binaries use C++14 (plugin-test.mk). Analysing the
# plugin as C++14 would let through constructs that will not compile in a real
# build, so the default matches the plugin.
CHECK_STD ?= c++11

# The test sources, when checked, need the standard they are built with.
CHECK_TEST_STD ?= c++14
CHECK_FLAGS := $(filter -I% -D%,$(FLAGS))

# Machine-generated sources: nobody edits them, and SirenTagClassifier.cpp alone is
# 89k lines and was half of all findings.
CHECK_EXCLUDE := src/modules/siren/SirenTagClassifier.cpp

CHECK_SOURCES := $(wildcard src/*.?pp src/**/*.?pp src/**/**/*.?pp)
CHECK_SOURCES := $(filter-out $(CHECK_EXCLUDE),$(CHECK_SOURCES))

ifdef STRICT
	CPPCHECK_EXIT := --error-exitcode=1
	TIDY_EXIT     := --warnings-as-errors=*
endif


.PHONY: cppcheck
cppcheck:
	cppcheck $(CHECK_SOURCES) $(CHECK_FLAGS) \
		--suppress=*:*dep/* --suppress=*:*src/test* \
		--std=$(CHECK_STD) --max-configs=1 \
		--enable=warning,style,performance,portability \
		-j 8 -q $(CPPCHECK_EXIT)


# clang-tidy on one file. Checks are configured in .clang-tidy at the repo root,
# so editors pick up the same set. Deliberately per-file: a whole-tree run needs a
# compile database and takes minutes, whereas this is the loop you actually use.
#
# A .test.cpp/.test.hpp is built as C++14 and needs src/test on the include path;
# everything else is plugin code and is checked as C++11, matching the real build.
TIDY_STD = $(if $(filter %.test.cpp %.test.hpp,$(FILE)),$(CHECK_TEST_STD),$(CHECK_STD))
TIDY_INC = $(if $(filter %.test.cpp %.test.hpp,$(FILE)),-Isrc/test -DDEBUGPLUGIN,)

.PHONY: tidy
tidy:
	@test -n "$(FILE)" || { echo "Usage: make tidy FILE=src/modules/dirt/Dirt.cpp"; exit 1; }
	clang-tidy $(TIDY_EXIT) $(FILE) -- -std=$(TIDY_STD) $(CHECK_FLAGS) $(TIDY_INC)
