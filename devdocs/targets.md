# Development Targets

A hand-written JSON parser in C11, built for practice. The goal is a parser that
is **correct against the spec**, **safe on hostile input**, and **simple enough to
read in one sitting** — in that order. Speed comes last.

Reference: [RFC 8259](https://www.rfc-editor.org/rfc/rfc8259) and the grammar at
[json.org](https://www.json.org/). When the two disagree with intuition, the RFC wins.

---

## Ground rules

- **No third-party code.** libc only. (Contrast with `hand-coded`, which leaned on libcsv.)
- **Arena allocation.** Every node and string for one document comes from a single
  `Arena`; freeing the document is one `arena_term()`. Reuse the arena from `hand-coded`.
- **Never read out of bounds.** The parser takes `(const char *buf, size_t len)` and
  never assumes a NUL terminator.
- **Errors, not crashes.** Every failure returns an error code with a byte offset,
  line, and column. No `abort()`, no `exit()` inside the library.
- **Build clean** with `-Wall -Wextra -Wpedantic` and run clean under ASan/UBSan
  (the default Debug build).

---

## Phase 0 — Scaffolding

Get the project shape right before writing parsing code.

- [ ] Copy `arena.c` / `arena.h` from `hand-coded` into `src/`
- [ ] Copy `file.c` / `file.h` (`file_read_all`) into `src/`
- [ ] Split the build: a `json` static library (`src/json.c`, `src/arena.c`, ...) and
      the `main` executable that links it, so tests can link the library too
- [ ] `main` usage: `main FILE` → parse it, print `ok` or the error, exit nonzero on failure
- [ ] Add `tests/` directory and a `test` CMake target (`ctest` or a plain test binary)

**Done when:** `main` reads a file and exits 0, and `ctest` runs (even with zero tests).

---

## Phase 1 — Data model and API

Design the public header before the parser. The API should look roughly like:

```c
typedef enum {
	JSON_NULL, JSON_BOOL, JSON_NUMBER, JSON_STRING, JSON_ARRAY, JSON_OBJECT
} JsonType;

typedef struct JsonValue JsonValue;

typedef struct {
	const char *key;      // NUL-terminated, UTF-8
	size_t key_len;       // keys may contain \u0000, so length is authoritative
	JsonValue *value;
} JsonMember;

struct JsonValue {
	JsonType type;
	union {
		bool boolean;
		double number;
		struct { const char *data; size_t len; } string;
		struct { JsonValue **items; size_t count; } array;
		struct { JsonMember *members; size_t count; } object;
	};
};

typedef enum {
	JSON_OK = 0,
	JSON_ERR_UNEXPECTED_EOF,
	JSON_ERR_UNEXPECTED_CHAR,
	JSON_ERR_INVALID_LITERAL,
	JSON_ERR_INVALID_NUMBER,
	JSON_ERR_INVALID_STRING,
	JSON_ERR_INVALID_ESCAPE,
	JSON_ERR_INVALID_UTF8,
	JSON_ERR_TRAILING_DATA,
	JSON_ERR_TOO_DEEP,
	JSON_ERR_OUT_OF_MEMORY,
} JsonStatus;

typedef struct {
	JsonStatus status;
	size_t offset;  // byte offset of the error
	size_t line;    // 1-based
	size_t column;  // 1-based, in bytes
} JsonError;

JsonStatus json_parse(const char *buf, size_t len, Arena *arena,
                      JsonValue **out, JsonError *err);
const char *json_status_str(JsonStatus status);
```

- [ ] `json.h` written with the types above (adjust as you learn)
- [ ] `json_status_str()` returns a human-readable message for each status
- [ ] Decide and document: does `json_parse` accept leading/trailing whitespace? (RFC: yes)

**Done when:** the header compiles and `main` calls a stub `json_parse` that returns
`JSON_ERR_UNEXPECTED_EOF`.

---

## Phase 2 — Literals and integers

Smallest end-to-end slice: a top-level scalar.

- [ ] Parser state struct: `buf`, `len`, `pos`, `line`, `col`, `arena`, `depth`
- [ ] `skip_whitespace()` — only space, `\t`, `\n`, `\r` (not `\f`, not `\v`)
- [ ] `null`, `true`, `false` — exact match; `nul`, `True`, `nullx` are errors
- [ ] Integers: optional `-`, then `0` or `[1-9][0-9]*`
- [ ] Reject leading zeros (`01`), lone `-`, leading `+`
- [ ] Anything after the value (other than whitespace) → `JSON_ERR_TRAILING_DATA`
- [ ] Empty input / whitespace-only input → `JSON_ERR_UNEXPECTED_EOF`

**Test inputs:** `null`, ` true `, `0`, `-0`, `42`, `-17` (accept);
`nul`, `01`, `-`, `+1`, `1 2`, `` (reject).

---

## Phase 3 — Strings (simple escapes)

- [ ] Opening/closing `"`; EOF before closing quote → `JSON_ERR_UNEXPECTED_EOF`
- [ ] Raw control characters (`0x00`–`0x1F`) inside a string are errors
- [ ] Escapes: `\"` `\\` `\/` `\b` `\f` `\n` `\r` `\t`
- [ ] Any other escape (`\a`, `\x41`, `\'`) → `JSON_ERR_INVALID_ESCAPE`
- [ ] Fast path: if a string has no escapes, copy it in one `memcpy`
- [ ] Decoded strings are stored NUL-terminated in the arena, with explicit length

**Test inputs:** `""`, `"abc"`, `"a\"b"`, `"tab\there"`, `"\/"` (accept);
`"abc`, `"\x"`, `"<raw newline>"` (reject).

---

## Phase 4 — Arrays and objects (recursive descent)

This is the core of the project.

- [ ] `parse_value()` dispatches on the first non-whitespace byte
- [ ] Arrays: `[`, zero or more values separated by `,`, `]`
- [ ] Objects: `{`, zero or more `"key": value` pairs separated by `,`, `}`
- [ ] Reject trailing commas: `[1,]`, `{"a":1,}`
- [ ] Reject missing commas/colons: `[1 2]`, `{"a" 1}`
- [ ] Object keys must be strings: `{a:1}`, `{1:1}` are errors
- [ ] Growing arrays: collect children in a temporary buffer, then copy into an exact-size
      arena allocation (or use `arena_grow_alloc`) — measure which is simpler
- [ ] **Depth limit** (e.g. 512): `[[[[...` 100 000 deep must return `JSON_ERR_TOO_DEEP`,
      not overflow the C stack
- [ ] Duplicate keys: decide a policy (keep all / keep last / reject) and document it.
      The RFC leaves this to the implementation.

**Test inputs:** `[]`, `{}`, `[1,[2,[3]]]`, `{"a":{"b":[true,null]}}` (accept);
`[1,]`, `[`, `{"a"}`, `{,}`, `[1 2]` (reject).

---

## Phase 5 — Unicode

- [ ] `\uXXXX` with exactly four hex digits (upper or lower case)
- [ ] Encode the decoded code point as UTF-8 (1–4 bytes)
- [ ] Surrogate pairs: `😀` → U+1F600 → 4 UTF-8 bytes
- [ ] Lone surrogates (`\uD800` alone, `\uDC00` first): pick a policy — reject, or
      replace with U+FFFD — and document it
- [ ] `\u0000` decodes to a real NUL byte inside the string (this is why `len` exists)
- [ ] Validate raw UTF-8 in the input: reject overlong encodings, invalid continuation
      bytes, code points above U+10FFFF, and encoded surrogates (`ED A0 80`)
- [ ] Optional: skip a leading UTF-8 BOM (`EF BB BF`) — RFC says parsers *may* ignore it

**Test inputs:** `"é"`, `"😀"`, `"é"` raw UTF-8 (accept);
`"\u12"`, `"\uZZZZ"`, `"\xC0\x80"` overlong (reject).

---

## Phase 6 — Full numbers

- [ ] Fraction: `.` followed by at least one digit (`1.` and `.5` are errors)
- [ ] Exponent: `e`/`E`, optional `+`/`-`, at least one digit (`1e`, `1e+` are errors)
- [ ] Validate the grammar yourself, then convert with `strtod` on a bounded copy
      (strtod accepts things JSON doesn't — `inf`, `nan`, hex, leading `+` — so never
      let it decide what is valid)
- [ ] Huge values (`1e400`): decide — reject, or store `±inf` — and document it
- [ ] Optional stretch: also keep the original number text so large integers
      (`12345678901234567890`) can be read without losing precision

**Test inputs:** `1.5`, `-0.0`, `1e10`, `1E-3`, `6.02e+23` (accept);
`1.`, `.5`, `1e`, `0x10`, `NaN`, `Infinity` (reject).

---

## Phase 7 — Conformance testing

- [ ] Vendor [JSONTestSuite](https://github.com/nst/JSONTestSuite)'s `test_parsing/`
      directory into `tests/JSONTestSuite/` (note its MIT license in the README)
- [ ] Test runner: for every file, `y_*` must parse, `n_*` must fail, `i_*` must not crash
- [ ] **100% of `y_` and `n_` tests pass**
- [ ] All `i_` tests run clean under ASan/UBSan; record which way you chose for each
- [ ] Unit tests for every error code, checking the reported line/column

---

## Phase 8 — Output and tooling

Prove the tree is correct by printing it back out.

- [ ] `json_write(const JsonValue *v, FILE *out, bool pretty)` — compact and indented modes
- [ ] Correct escaping on output: `"`, `\`, control characters as `\u00XX`
- [ ] Round-trip test: `parse → write → parse → write` produces identical output
- [ ] `main` gains flags, in the style of `hand-coded`:
      `main --json PATH [--pretty | --compact] [--quiet]`
- [ ] Error output points at the problem:
      `error: unexpected ',' at line 3, column 12`

---

## Phase 9 — Robustness and performance

- [ ] Fuzz with libFuzzer (`clang -fsanitize=fuzzer,address`) for at least an hour with
      no crashes; add any crashing inputs as regression tests
- [ ] Generate a large test file (~50 MB, similar to `sample_large.csv`) and benchmark
      a Release build
- [ ] Compare against an established library (cJSON or yyjson) for speed and memory —
      for perspective, not to win
- [ ] Profile with `perf` and fix the single biggest hotspot

---

## Stretch goals

Pick any after Phase 8:

- **Lookup helpers:** `json_object_get(v, "key")`, `json_array_at(v, i)`,
  a path query like `json_path(v, "users[0].name")`
- **Streaming / SAX API:** callbacks per token instead of building a tree —
  same design as libcsv's `cb_field` / `cb_row`
- **Zero-copy strings:** point into the input buffer when a string has no escapes
- **Better error messages:** print the offending line with a `^` caret underneath
- **Scala port:** rewrite it in Scala in `scala_practice` and compare the source,
  as with the CSV reader

---

## Decisions log

Record policy choices here as you make them.

| Question | Decision | Why |
|---|---|---|
| Duplicate object keys | | |
| Lone surrogates in `\u` escapes | | |
| Numbers out of `double` range | | |
| Leading UTF-8 BOM | | |
| Maximum nesting depth | | |
