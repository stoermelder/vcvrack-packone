"""Emit the C++ source file containing the trained model.

The output is a complete `.cpp` — there is no surrounding plugin header
to paste into. The whole file is the model: it includes
`SirenTagClassifierApi.hpp`, emits the per-class m2cgen bodies + uniform
bridges + Platt-scaled dispatcher, and self-registers via a static
initializer (lazy, via `_setLoader()`).

`m2cgen` transpiles a fitted `sklearn` model into plain C. For a
`MultiOutputClassifier` wrapping a binary `RandomForestClassifier` (our
default), each per-class estimator is either:

  - 2-class  ->  ``void <name>(double *input, double *output)``
                ``output[0] = P(label=0)``, ``output[1] = P(label=1)``
  - 1-class  ->  ``double <name>(double *input)``
                (degenerate case where all training samples for that class
                shared the same label)

The Python scoring side (`classify_wav.py`) uses
`predict_proba(...)[0, 1]` -- i.e. the probability of the **positive**
class. So for the 2-class shape we want `output[1]`, not `output[0]`.

We wrap each m2cgen body in a uniform bridge function
`static double siren_tag_score_class_N(double *input)` that returns the
class-N probability (positive-class probability for 2-class, the value
itself for 1-class). The dispatcher can then uniformly do
`double _s = siren_tag_score_class_N(_x);` for every class.

The result is a source file that compiles cleanly into the plugin and gives
identical scores to the Python `classify_wav.py` reference at the same
`MODEL_VERSION`.
"""
from __future__ import annotations

import re
from pathlib import Path

import m2cgen as m2c

from feature_config import CLASS_NAMES, MODEL_VERSION, NUM_CLASSES, NUM_FEATURES


# Helper functions that m2cgen emits alongside each per-class body. They're
# identical in every block, so we strip them from each block and emit them
# once at the top of the file.
_M2C_HELPERS_PATTERN = re.compile(
    r"^void\s+add_vectors\s*\([^)]*\)\s*\{[^}]*\}\s*\n"
    r"^void\s+mul_vector_number\s*\([^)]*\)\s*\{[^}]*\}\s*\n",
    flags=re.M,
)


def _strip_helpers_and_static(code: str) -> str:
    """Drop the local ``add_vectors`` / ``mul_vector_number`` helpers and the
    leading ``static`` qualifier on the per-class function.

    m2cgen emits ``static double <name>(...)`` for 1-class estimators and
    ``static void <name>(...)`` for 2-class. We de-staticize so the bridges
    (in the same TU) can call them. The helpers are emitted at the top of
    the file once instead of repeated in every per-class block.
    """
    code = _M2C_HELPERS_PATTERN.sub("", code)
    code = re.sub(r"^static\s+double\s+", "double ", code, flags=re.M)
    code = re.sub(r"^static\s+void\s+",   "void ",   code, flags=re.M)
    return code


def _shape(estimator) -> str:
    """Return ``"single"`` (1-class, returns double) or ``"binary"`` (2+ class, void return, 2-element output)."""
    classes = getattr(estimator, "classes_", None)
    if classes is None or len(classes) < 2:
        return "single"
    return "binary"


def _emit_bridge(c: int, shape: str, impl_name: str) -> str:
    """Wrap the m2cgen body ``<impl_name>(double *input[, double *output])``
    into a uniform ``static double siren_tag_score_class_<c>(double *input)``
    that returns the class-N (positive-class) probability.

    For 1-class estimators the body returns the value directly; the bridge
    forwards it.

    For 2-class estimators the body writes ``output[0] = P(label=0)``,
    ``output[1] = P(label=1)``; we take ``output[1]`` as the class-N score
    so the C++ runtime matches the Python ``predict_proba(...)[0, 1]`` call.
    """
    if shape == "single":
        return (
            f"static double siren_tag_score_class_{c}(double * input) {{\n"
            f"    return {impl_name}(input);\n"
            f"}}\n"
        )
    return (
        f"static double siren_tag_score_class_{c}(double * input) {{\n"
        f"    double _out[2] = {{0.0, 0.0}};\n"
        f"    {impl_name}(input, _out);\n"
        f"    return _out[1];  // P(label=1) = class-{c} probability\n"
        f"}}\n"
    )


def emit_cpp(
    model,
    out_path: Path,
    calibration_params: list[tuple[float, float] | None] | None = None,
) -> None:
    """Write the generated C++ source file to ``out_path``.

    The result is consumed by ``src/modules/siren/SirenTagClassifier.cpp``
    (the whole file IS the model — the developer copies it over after
    running ``run.sh``).
    """
    if not hasattr(model, "estimators_"):
        raise ValueError(
            "emit_cpp expects a MultiOutputClassifier (or any object exposing "
            "`.estimators_`). Wrap with sklearn.multioutput.MultiOutputClassifier."
        )

    # Render each per-class estimator as its own C body, then wrap each in a
    # uniform bridge. The m2cgen body is renamed to `siren_tag_class_<c>_impl`
    # so multiple instances don't collide on their default `score` name.
    per_class_blocks: list[str] = []
    per_class_bridges: list[str] = []
    per_class_shapes: list[str] = []

    impl_name = "siren_tag_class_{c}_impl"

    for c in range(NUM_CLASSES):
        estimator = model.estimators_[c]
        shape = _shape(estimator)
        per_class_shapes.append(shape)
        iname = impl_name.format(c=c)
        body = m2c.export_to_c(estimator, function_name=iname)
        body = _strip_helpers_and_static(body)
        per_class_blocks.append(body.rstrip())
        per_class_bridges.append(_emit_bridge(c, shape, iname))

    # Build the dispatcher body. Each bridge has a uniform signature
    # `double siren_tag_score_class_c(double *input)`, so the dispatcher
    # is identical for every class.
    # Use literals (not SIREN_TAG_NUM_FEATURES) so the model body compiles
    # standalone without including SirenTagClassifierAPI.hpp.
    body_lines: list[str] = []
    body_lines.append("        // Per-class scores from the trained sklearn model.")
    body_lines.append("        // m2cgen-generated; do not edit by hand -- re-run scripts/siren-tag-model/run.sh to refresh.")
    body_lines.append(f"        double _x[{NUM_FEATURES}];")
    body_lines.append(f"        for (int _i = 0; _i < {NUM_FEATURES}; ++_i) _x[_i] = (double)features_in[_i];")
    body_lines.append("        (void)_x;")
    body_lines.append("")

    for c in range(NUM_CLASSES):
        cal = calibration_params[c] if calibration_params else None
        if cal is not None:
            a, b = cal
            # Platt convention: p = 1 / (1 + exp(a*raw + b)), a is typically negative
            score_expr = f"1.0 / (1.0 + exp({a:.8f} * _raw + {b:.8f}))"
            cal_comment = f"Platt: 1/(1+exp({a:.4f}*raw+{b:.4f}))"
        else:
            score_expr = "_raw"
            cal_comment = "no calibration (single label in cal set)"
        body_lines.append(f"        {{ // class {c} = {CLASS_NAMES[c]!r}  [{cal_comment}]")
        body_lines.append(f"            double _raw = siren_tag_score_class_{c}(_x);")
        body_lines.append(f"            double _s   = {score_expr};")
        body_lines.append(f"            scores_out[{c}] = (float)_s;")
        body_lines.append("        }")
        body_lines.append("")

    # Compose the full generated source file.
    parts: list[str] = []
    parts.append("// AUTO-GENERATED by scripts/siren-tag-model/emit_cpp.py via m2cgen.")
    parts.append("// DO NOT EDIT BY HAND -- re-run scripts/siren-tag-model/run.sh to refresh.")
    parts.append(f"// Model version: {MODEL_VERSION}")
    parts.append(f"// Classes ({NUM_CLASSES}): {', '.join(CLASS_NAMES)}")
    parts.append(f"// Features ({NUM_FEATURES}): see feature_config.FEATURE_NAMES")
    parts.append("")
    parts.append('#include "SirenTagClassifierApi.hpp"  // defines TagClassifier, SIREN_TAG_NUM_FEATURES')
    parts.append("#include <cmath>    // for exp() in Platt sigmoid calibration")
    parts.append("#include <cstddef>")
    parts.append("#include <cstring>  // for memcpy in m2cgen-emitted bodies")
    parts.append("")
    # SIREN_TAG_NUM_FEATURES is defined in SirenTagClassifierAPI.hpp (not here).
    # This assert fires if the model was trained with a different feature count.
    parts.append(f"static_assert(::StoermelderPackOne::Siren::SIREN_TAG_NUM_FEATURES == {NUM_FEATURES},")
    parts.append(f'    "Model was trained with {NUM_FEATURES} features; re-run scripts/siren-tag-model/run.sh");')
    parts.append(f"static const int SIREN_TAG_NUM_CLASSES   = {NUM_CLASSES};")
    parts.append(f"static const int SIREN_TAG_MODEL_VERSION = {MODEL_VERSION};")
    parts.append("")
    parts.append("static const char* const SIREN_TAG_CLASS_NAMES[SIREN_TAG_NUM_CLASSES] = {")
    for name in CLASS_NAMES:
        parts.append(f'    "{name}",')
    parts.append("};")
    parts.append("")

    # Emit each per-class m2cgen body. The helpers (`add_vectors` /
    # `mul_vector_number`) were stripped from every block in
    # `_strip_helpers_and_static`; we emit them once here.
    parts.append("// Shared vector helpers used by every per-class body below.")
    parts.append("// (m2cgen emits these inline per body; we hoist them so they")
    parts.append("// aren't redefined 18 times.)")
    parts.append("static void add_vectors(double *v1, double *v2, int size, double *result) {")
    parts.append("    for (int i = 0; i < size; ++i) result[i] = v1[i] + v2[i];")
    parts.append("}")
    parts.append("static void mul_vector_number(double *v1, double num, int size, double *result) {")
    parts.append("    for (int i = 0; i < size; ++i) result[i] = v1[i] * num;")
    parts.append("}")
    parts.append("")
    parts.append("// Per-class m2cgen bodies -- one C function per binary classifier.")
    parts.append("// Each function is wrapped in a uniform bridge below.")
    parts.append("")

    for c, block in enumerate(per_class_blocks):
        # Strip the local `#include <string.h>` -- we hoisted it to the top.
        block = re.sub(r"^\s*#\s*include\s*<string\.h>\s*\n", "", block, flags=re.M)
        parts.append(block.rstrip())
        parts.append(f"// shape class {c} = {per_class_shapes[c]}")
        parts.append("")

    # Emit the bridge functions, one per class, in order.
    parts.append("// Per-class bridges -- uniform signature `double f(double *input)`")
    parts.append("// that returns the class-N (positive-class) probability.")
    for bridge in per_class_bridges:
        parts.append(bridge.rstrip())
        parts.append("")

    # Dispatcher: plain pointer types so the model compiles standalone.
    parts.append("// Compute per-class scores. Output is in [0, 1] (positive-class probability per binary classifier).")
    parts.append("// Registered with TagClassifier via the anonymous namespace below.")
    parts.append("static void siren_tag_score(const float* features_in, float* scores_out) {")
    parts.extend(body_lines)
    parts.append("}")
    parts.append("")
    parts.append("// Register a deferred loader so the model is wired up on first scoring use,")
    parts.append("// not at static-init time. Cost at dylib load: one pointer store.")
    parts.append("static void _siren_load_model() {")
    parts.append("    ::StoermelderPackOne::Siren::TagClassifier::registerModel(")
    parts.append("        siren_tag_score, SIREN_TAG_NUM_CLASSES, SIREN_TAG_CLASS_NAMES);")
    parts.append("}")
    parts.append("namespace {")
    parts.append("    static const bool _siren_loader_set =")
    parts.append("        (::StoermelderPackOne::Siren::TagClassifier::_setLoader(_siren_load_model), true);")
    parts.append("}")

    out_path.write_text("\n".join(parts) + "\n")
