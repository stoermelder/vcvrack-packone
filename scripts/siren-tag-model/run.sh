#!/usr/bin/env bash
# Generate a synthetic dataset, train a tiny model, and emit a C++ header
# fragment you paste into src/modules/Siren/SirenTagClassifier.hpp.
#
# Usage:
#   bash scripts/siren-tag-model/run.sh
#   bash scripts/siren-tag-model/run.sh --n-per-class 200
#   bash scripts/siren-tag-model/run.sh --csv my_real_dataset.csv
#
# After it finishes, the generated body is in:
#   scripts/siren-tag-model/build/SirenTagClassifier.generated.hpp
# Copy it into the marked region of the C++ header and rebuild.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

VENV_DIR="$SCRIPT_DIR/.venv"

# 1. Set up venv + install deps (cached after first run)
if [[ ! -d "$VENV_DIR" ]]; then
  echo "▶ Creating Python venv at $VENV_DIR"
  python3 -m venv "$VENV_DIR"
fi
# shellcheck disable=SC1091
source "$VENV_DIR/bin/activate"
echo "▶ Installing requirements (this may take a minute the first time) ..."
pip install --quiet --upgrade pip
pip install --quiet -r "$SCRIPT_DIR/requirements.txt"

# 2. Build the C++ feature extractor.
echo "▶ Building C++ feature extractor ..."
make -C "$SCRIPT_DIR" ${RACK_DIR:+RACK_DIR="$RACK_DIR"}

# 3. Parse --csv if provided, else generate synthetic dataset.
CSV_FLAG=()
N_PER_CLASS=80
EXTRA_ARGS=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    --csv=*)        CSV_FLAG=(--csv "${1#*=}"); shift ;;
    --csv)          CSV_FLAG=(--csv "$2");      shift 2 ;;
    --n-per-class=*) N_PER_CLASS="${1#*=}";    shift ;;
    --n-per-class)  N_PER_CLASS="$2";          shift 2 ;;
    *)              EXTRA_ARGS+=("$1");         shift ;;
  esac
done

if [[ ${#CSV_FLAG[@]} -eq 0 ]]; then
  echo "▶ Generating synthetic dataset (n_per_class=$N_PER_CLASS) ..."
  mkdir -p build
  python3 generate_synthetic_dataset.py --out build --n-per-class "$N_PER_CLASS"
  CSV_FLAG=(--csv build/synthetic_dataset.csv)
fi

# 4. Train + emit C++ header fragment.
echo "▶ Training model and emitting C++ header ..."
python3 train_model.py "${CSV_FLAG[@]}" ${EXTRA_ARGS[@]+"${EXTRA_ARGS[@]}"}

echo
echo "── Done ────────────────────────────────────────────────────────"
echo "Generated header fragment is at:"
echo "    $SCRIPT_DIR/build/SirenTagClassifier.generated.hpp"
echo
echo "Next steps:"
echo "  1. Open the generated file above."
echo "  2. Copy its contents into the marked region of"
echo "        src/modules/Siren/SirenTagClassifier.hpp"
echo "  3. Bump SIREN_TAG_MODEL_VERSION in feature_config.py if the"
echo "     shape (number of features / classes) changed."
echo "  4. Rebuild the plugin:  cd .. && make"
echo
