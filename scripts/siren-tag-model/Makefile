PLUGIN_ROOT := ../..
RACK_DIR    ?= $(PLUGIN_ROOT)/../..

CXX ?= c++
CC  ?= cc

INCLUDES := \
	-I$(PLUGIN_ROOT)/src/modules/siren \
	-I$(PLUGIN_ROOT)/dep/drlibs \
	-I$(RACK_DIR)/dep/pffft \
	-I$(RACK_DIR)/dep/include

OUT := build/siren_extract_features

.PHONY: all clean

all: $(OUT)

build/pffft.o: $(RACK_DIR)/dep/pffft/pffft.c | build
	$(CC) -O2 $(INCLUDES) -c $< -o $@

$(OUT): siren_extract_features.cpp build/pffft.o | build
	$(CXX) -std=c++11 -O2 $(INCLUDES) -o $@ $^ -lm

build:
	mkdir -p build

clean:
	rm -f $(OUT) build/pffft.o

# ──────────────────────────────────────────────────────────────────────────
# Python-orchestrated pipeline targets
#
# This block layers a `make`-driven workflow on top of the Python scripts
# (generate_synthetic_dataset.py, load_folder_dataset.py, train_model.py,
# classify_wav.py). It mirrors what run.sh / run.bat do, but with three
# wins:
#
#   1. Incremental. The venv is created only when missing, the dataset
#      is regenerated only when the generator script or its inputs change,
#      and pip-installs are skipped when the venv is up to date.
#   2. OS-portable dispatch. PY / VENV_PY resolve to the right Python
#      binary on macOS/Linux/Windows without the user picking the script.
#   3. Make-native overrides. `make train CSV=build/my_samples.csv
#      N_ESTIMATORS=60` instead of `--csv build/my_samples.csv
#      --n-estimators 60`. The underlying script flags are unchanged.
#
# run.sh and run.bat are kept as the no-Make fallback for users who don't
# have GNU make on PATH (notably some Windows setups without MSYS /
# Git-Bash). They delegate to the same Python scripts.
# ──────────────────────────────────────────────────────────────────────────

# ── OS detection ─────────────────────────────────────────────────────────
# `OS` is a builtin on Windows nmake; on macOS/Linux it's empty so we fall
# back to `uname -s`. The result drives PY / VENV_PY so the same recipe
# works in Git-Bash, WSL, and native Windows shells.
ifeq ($(OS),Windows_NT)
  HOST_OS := windows
  VENV := .venv
  VENV_PY := $(VENV)/Scripts/python.exe
  MKDIR := if not exist "$(1)" mkdir "$(1)"
  RM_F  := del /F /Q
  NULL  := nul
else
  HOST_OS := $(shell uname -s 2>/dev/null || echo unknown)
  VENV := .venv
  VENV_PY := $(VENV)/bin/python
  MKDIR := mkdir -p $(1)
  RM_F  := rm -f
  NULL  := /dev/null
endif

PY        ?= $(VENV_PY)
PIP       ?= $(VENV_PY) -m pip

# Default parameter values for the training / dataset / classify targets.
# Override on the command line, e.g. `make train CSV=build/my.csv`.
N_PER_CLASS    ?= 80
SEED           ?= 42
N_ESTIMATORS   ?= 24
MAX_DEPTH      ?= 8
AUGMENT        ?= 3
NEGATIVE_WEIGHT ?= 3.0
TOP_K          ?= 3
DATASET_OUT    ?= build/synthetic_dataset.csv
TRAIN_OUT      ?= build/SirenTagClassifier.generated.cpp
LOADER_OUT     ?= build/my_dataset.csv
CLASSIFY_CSV   ?= build/synthetic_dataset.csv

# ── venv lifecycle ───────────────────────────────────────────────────────
.PHONY: venv
venv: $(VENV_PY)

$(VENV_PY): requirements.txt
	@echo "▶ Creating Python venv at $(VENV) (one-time, ~1 minute) ..."
	@test -d $(VENV) || python3 -m venv $(VENV)
	$(PIP) install --quiet --upgrade pip
	$(PIP) install --quiet -r requirements.txt
	@touch $(VENV_PY)

# ── C++ feature extractor (already-built target, exposed as a file rule) ─
.PHONY: extractor
extractor: $(OUT)

# ── Synthetic dataset generation ─────────────────────────────────────────
# Regenerated when the generator script or its parameters change. The
# dataset is also a prerequisite for `train` (unless CSV is overridden).
.PHONY: dataset
dataset: $(DATASET_OUT)

$(DATASET_OUT): generate_synthetic_dataset.py feature_config.py tag_manifest.py features.py $(OUT) | build
	@echo "▶ Generating synthetic dataset (n_per_class=$(N_PER_CLASS), seed=$(SEED)) ..."
	$(PY) generate_synthetic_dataset.py --out build --n-per-class $(N_PER_CLASS) --seed $(SEED)

# ── Folder-dataset → CSV loader ──────────────────────────────────────────
# Walk a tree of <Tag>/ and Non-<Tag>/ subfolders, extract features with
# the C++ binary, write a CSV. Used for real-world sample collections.
# Usage: make csv-from-folder ROOT=path/to/my_samples
.PHONY: csv-from-folder
csv-from-folder: $(OUT)
	@if [ -z "$(ROOT)" ]; then \
		echo "error: ROOT is required, e.g. make csv-from-folder ROOT=path/to/my_samples" 1>&2; \
		exit 2; \
	fi
	@echo "▶ Loading folder dataset from $(ROOT) ..."
	$(PY) load_folder_dataset.py $(ROOT) --out $(LOADER_OUT) \
		$(if $(MAX_PER_CLASS),--max-per-class $(MAX_PER_CLASS))

# ── Train the model and emit SirenTagClassifier.generated.cpp ───────────
# Required: CSV. The dataset rule above will have produced the default
# synthetic CSV; for real data pass CSV=build/my_samples.csv explicitly.
# Usage: make train CSV=build/my_samples.csv N_ESTIMATORS=60 NEGATIVE_WEIGHT=5.0
.PHONY: train
train:
	@if [ -z "$(CSV)" ]; then \
		echo "error: CSV is required, e.g. make train CSV=build/my_samples.csv" 1>&2; \
		echo "       (or: make train                          # uses the synthetic dataset)" 1>&2; \
		exit 2; \
	fi
	@echo "▶ Training RandomForest on $(CSV) ..."
	$(PY) train_model.py --csv "$(CSV)" --out $(TRAIN_OUT) \
		--n-estimators $(N_ESTIMATORS) --max-depth $(MAX_DEPTH) \
		--augment $(AUGMENT) --seed $(SEED) --negative-weight $(NEGATIVE_WEIGHT)

# ── Score a single audio file against the trained model ─────────────────
# Usage: make classify WAV=path/to/snippet.wav
.PHONY: classify
classify:
	@if [ -z "$(WAV)" ]; then \
		echo "error: WAV is required, e.g. make classify WAV=path/to/snippet.wav" 1>&2; \
		exit 2; \
	fi
	@echo "▶ Classifying $(WAV) ..."
	$(PY) classify_wav.py "$(WAV)" --top-k $(TOP_K) --csv $(CLASSIFY_CSV) \
		$(if $(NO_MODEL),--no-model)

# ── Full pipeline (mirrors run.sh / run.bat) ─────────────────────────────
# Order: ensure extractor is built, generate dataset (or skip if CSV is
# given explicitly), then train and emit C++. Forward any unrecognised
# variables to `train` by setting them on the make command line.
.PHONY: pipeline
pipeline: $(OUT)
	@if [ -n "$(ROOT)" ]; then \
		echo "▶ Pipeline (folder mode: ROOT=$(ROOT))"; \
		$(MAKE) csv-from-folder ROOT=$(ROOT) MAX_PER_CLASS=$(MAX_PER_CLASS) --no-print-directory; \
		CSV=$(LOADER_OUT) $(MAKE) train --no-print-directory \
			CSV=$(LOADER_OUT) \
			N_ESTIMATORS=$(N_ESTIMATORS) MAX_DEPTH=$(MAX_DEPTH) \
			AUGMENT=$(AUGMENT) SEED=$(SEED) NEGATIVE_WEIGHT=$(NEGATIVE_WEIGHT); \
	elif [ -n "$(CSV)" ]; then \
		echo "▶ Pipeline (csv mode: CSV=$(CSV))"; \
		$(MAKE) train --no-print-directory \
			CSV=$(CSV) \
			N_ESTIMATORS=$(N_ESTIMATORS) MAX_DEPTH=$(MAX_DEPTH) \
			AUGMENT=$(AUGMENT) SEED=$(SEED) NEGATIVE_WEIGHT=$(NEGATIVE_WEIGHT); \
	else \
		echo "▶ Pipeline (synthetic-data mode: n_per_class=$(N_PER_CLASS))"; \
		$(MAKE) dataset --no-print-directory N_PER_CLASS=$(N_PER_CLASS) SEED=$(SEED); \
		$(MAKE) train --no-print-directory \
			CSV=$(DATASET_OUT) \
			N_ESTIMATORS=$(N_ESTIMATORS) MAX_DEPTH=$(MAX_DEPTH) \
			AUGMENT=$(AUGMENT) SEED=$(SEED) NEGATIVE_WEIGHT=$(NEGATIVE_WEIGHT); \
	fi
	@echo
	@echo "── Done ────────────────────────────────────────────────────────"
	@echo "Generated model:  $(TRAIN_OUT)"
	@echo "Next: copy it into src/modules/siren/SirenTagClassifier.cpp"
	@echo "       and rebuild the plugin (cd $(PLUGIN_ROOT) && make)."

# ── Test the loader's folder-name parsers (no C++ extractor required) ──
.PHONY: self-test
self-test:
	$(PY) load_folder_dataset.py --self-test

# ── Updated `clean` ─────────────────────────────────────────────────────
# Now removes the dataset, the generated model, and (optionally) the venv.
.PHONY: clean-all
clean-all: clean
	-$(RM_F) $(DATASET_OUT) $(TRAIN_OUT) $(LOADER_OUT)
	-$(RM_F) build/synthetic_audio/*.wav
	-rm -rf $(VENV) 2>$(NULL) || true
