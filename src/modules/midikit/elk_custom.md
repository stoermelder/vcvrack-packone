# Elk differences vs upstream

This document summarizes the differences between the vendored copy of the
[cesanta/elk](https://github.com/cesanta/elk) JavaScript engine used by the
Elk scripting backend of the Midi-Kit module and the upstream reference below.
It lists only what is **not** present in that upstream version; anything that
already exists upstream is omitted.

- Vendored files: [`elk.c`](elk.c), [`elk.h`](elk.h)
- Upstream reference: `cesanta/elk` @ `71a86fa2fef146696be9ae66715bf3f91d0a5f2c`
  (master, `JS_VERSION "3.0.0"`, 2026-03-18 — merge of PR #77 "setprop-bug-fix")
- License: AGPL-3.0 / commercial (Cesanta) — kept intact in the header
- Tests: [`elk_unit_test.c`](elk_unit_test.c)

Each difference is annotated in the source with a `// stoermelder:` comment so
it can be told apart from upstream code at a glance.

---

## Features (not in upstream)

### 1. JavaScript array support
Upstream Elk has no array literals or subscripting. This fork adds a minimal but
usable array implementation built on Elk's object/property model.

- **Lexer** (`next()` in `elk.c`): new tokens `TOK_LBRACKET` / `TOK_RBRACKET`
  and cases for `[` / `]`.
- **`js_array_literal()`**: parses `[a, b, c]`. An array is stored as an object
  whose elements are numeric-indexed properties, plus a `length` property that
  holds the element count.
- **`js_literal()`**: dispatches `TOK_LBRACKET` to the new array literal.
- **`js_call_dot()`**: handles `obj[...]` subscripting. A numeric index is
  converted to a string key and routed through the existing DOT operator; the
  new `F_NUMIDX` flag marks that the operation came from a numeric index.
- **Auto-growing `length`**: assigning to a numeric index (e.g. `h[10] = 5`)
  bumps `length` if the index is past the end (`h.length` becomes `11`).
  Negative indices are ignored (`n[-1] = 2` leaves `length` at `0`).
- **`length` is read-only on arrays**: assigning `arr.length = 1` raises
  `ERROR: cannot set array length`. Setting `length` on a plain object still
  works.
- **Stringify**: `strobj()` skips printing the `length` property when it is
  numeric zero, so an empty array stringifies as `{}` while `a.length` is still
  `0` at runtime. Non-empty arrays stringify with `length` included,
  e.g. `[1,2,3]` → `{"length":3,"2":3,"1":2,"0":1}`.

### 2. Error position reporting — `js_errpos()`
Upstream Elk reports errors as a bare string (`"ERROR: parse error"`) with no
indication of *where* the failure happened. This fork records the source offset.

- New `jsoff_t errpos` field in `struct js`.
- `js_mkerr()` captures `js->toff` (the offset of the last token `next()`
  scanned — always set before any error path runs) into `errpos` *before*
  jumping the parser to the end of the buffer to abort. Only the first error of
  an eval is kept, since subsequent errors would otherwise overwrite it with a
  useless end-of-buffer offset.
- `errpos` is reset to `~0` ("no error yet") in `js_create()` and at the start
  of every `js_eval()`.
- New public API `size_t js_errpos(struct js *)` returns the captured offset, or
  `~0` when there is none.
- Consumed by `MidiScriptEngineElk::formatError()` to render script errors as
  `line:col: <message>  > <source line>`, so script authors can find the
  offending line without an external editor.

### 3. Live memory usage — `js_usage()`
New public API `size_t js_usage(struct js *)` returns `js->brk`, the current
allocated JS memory in bytes. This differs from `js_stats()`'s `lwm`, which is
a historical low-water mark (peak usage), not the live value. Used by the
Midi-Kit UI to display current script RAM usage.

### 4. Boolean equality — `===` / `!==`
`do_op()` now allows `===` / `!==` between two booleans, compared as `0`/`1`
(so `flag === true`, `flag !== false`, `true === true` work). Mixed
boolean/number or boolean/string comparisons still fall through to the existing
`type mismatch` rejection.

---

## Bug fixes (not in upstream)

### 5. Bare `return;` early-return bug
A bare `return;` (with no value) returned `undefined` without signaling that a
return had executed — it neither moved `js->pos` to the end of the snippet nor
set `F_RETURN`. Now a bare `return;` behaves like `return expr;`: it exits the
code snippet and tells the caller via `F_RETURN`.

### 6. Missing parse error on unterminated function bodies
`js_block()` stops on EOF *or* `}` without saying which, so an unterminated
function body (`let f = function(x) {`) reached the end of input and looked
like a clean block, loading as a valid script. `js_func_literal()` now demands
the closing `}` and raises `ERROR: } expected` when it is missing.

### 7. Garbage collection bug during nested calls
Elk's GC is documented to run "before every top-level statement", and the rest
of the engine is written against that invariant: `js_gc()` compacts live
entities and `js_fixup_offsets()` only repairs state reachable from `struct js`
(`js->scope`, `js->nogc`, and the single live `js->code`). Anything a
*suspended* frame holds is invisible to it.

But `js_stmt()` is not only the top-level statement loop — it recurses for every
statement inside function bodies and blocks. Without a guard, a collection could
fire deep inside nested calls (e.g. the Scale quantiser's
`onMidiMessage() -> quantise()`) while `do_call_op()` still holds the caller's
`js->code` as a plain pointer and `call_js()` holds the callee's body. Compaction
then shifted memory out from under both frames, surfacing as spurious
`parse error` or `'x' not found` on whichever call first pushed `brk` past `gct`.

Fix: collection now runs only at a **top-level** statement boundary, by
deferring while `F_CALL` is set (which is set for exactly the duration of a
function call):

```c
if (js->brk > js->gct && !(js->flags & F_CALL)) js_gc(js);
```

The allocation is not lost — `brk` stays above `gct`, so the very next
top-level statement collects instead.

### 8. Build warning suppression
`js_eval()` takes the address of its local `res` into `js->cstk`, which newer
GCC flags with `-Wdangling-pointer`. The warning is suppressed around that
assignment with a `#pragma GCC diagnostic ignored` block, guarded so it only
applies to GCC and not clang (which would error on the unknown pragma).

---

## Public API surface added to `elk.h`

| Function | Purpose |
| --- | --- |
| `size_t js_errpos(struct js *)` | Byte offset in the code buffer of the last error, or `~0` if none (feature 2). |
| `size_t js_usage(struct js *)` | Current live JS memory usage in bytes (`js->brk`) (feature 3). |

---

## Tests

`elk_unit_test.c` covers all of the above, including dedicated suites
`test_arrays()` (array literals, index assignment, auto-growing/read-only
`length`, nested arrays, string indices, non-existent indices), `test_bool()`
(boolean `===`/`!==`), `test_gc_during_call()` (collection inside nested calls),
and error cases such as an unterminated function body being a parse error.
