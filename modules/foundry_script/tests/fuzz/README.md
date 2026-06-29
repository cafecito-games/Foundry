# Foundry Script parser fuzzing (libFuzzer prototype)

A coverage-guided libFuzzer harness that drives `FSParser::parse()` (and,
on a clean parse, `FSAnalyzer::analyze()`) over mutated input. The parser's
hard contract is that *any* byte sequence yields either a clean error list or a
valid tree — never a crash. The editor and LSP parse untrusted/partial `.fs`
buffers, so robustness here is a real concern.

When `use_fuzzer=yes` is passed, the platform `main()` is replaced by a small
libFuzzer entry point. The build is split so the interesting logic is shared:

- `modules/foundry_script/tests/fuzz/gdscript_fuzzer.{h,cpp}` — platform-independent:
  engine bring-up plus `LLVMFuzzerTestOneInput` (the parse/analyze loop).
  Compiled into the Foundry Script module only when `use_fuzzer=yes`.
- `platform/<platform>/fuzz_gdscript_<platform>.{cpp,mm}` — a thin shim that
  constructs the headless OS and calls into the shared harness.

## Building

### Linux (used by CI)

libFuzzer ships with LLVM, so `use_fuzzer` requires `use_llvm=yes`; the runtime
is linked automatically by `-fsanitize=fuzzer`.

```sh
scons platform=linuxbsd target=editor dev_build=yes \
  use_fuzzer=yes use_llvm=yes use_asan=yes -j$(nproc)
# -> bin/godot.linuxbsd.editor.dev.x86_64.san.fuzz
```

### macOS

Apple's clang does not ship the libFuzzer runtime, so the engine is compiled
with Apple clang and linked against the runtime from a Homebrew LLVM of a
matching major version (`brew install llvm`); the build picks it up
automatically.

```sh
scons platform=macos target=editor dev_build=yes use_fuzzer=yes use_asan=yes -j$(sysctl -n hw.ncpu)
# -> bin/godot.macos.editor.dev.<arch>.san.fuzz
```

`target=editor` is required on both platforms: export-template builds exit at
startup without a `.pck`, while the editor (tool) build boots without a project.

## Running

```sh
# Seed corpus from the existing script fixtures (a keyword dictionary ships here).
mkdir -p corpus && find ../scripts -name '*.fs' -exec cp {} corpus/ \;

ASAN_OPTIONS=detect_leaks=0 \
  <path-to>/godot.<platform>...san.fuzz \
  -dict=gdscript.dict -timeout=25 -rss_limit_mb=4096 \
  -jobs=6 -workers=6 corpus
```

Observed: ~310 exec/s/worker parser-only, ~95–200 exec/s/worker with the
analyzer; ~6.5k edges parser-only rising to ~19k edges with the analyzer pass.

**macOS only — add `-reduce_inputs=0 -shrink=0`.** On macOS the corpus-reduction
path (`InputCorpus::Replace`) crashes after a few hundred iterations inside
libFuzzer's own `std::string` bookkeeping — an artifact of mixing Apple clang's
AddressSanitizer runtime with the Homebrew LLVM libFuzzer runtime, not a Godot
bug (with reduction disabled the same build runs 30k+ iterations clean). The
Linux/CI build uses a single LLVM toolchain for both ASan and libFuzzer, so this
does not occur there and input reduction can be left on.

## Harness notes

The shared harness boots the engine once via `Main::setup(..., /*second_phase=*/
true)`. The platform shim chooses a headless OS (`OS_MacOS_Headless`, since
`OS_MacOS_NSApp::run()` drives a Cocoa loop that never returns; `OS_LinuxBSD`
with `--headless`). Two engine interactions matter for any port:

- Godot installs its own crash handler during setup, which shadows the
  libFuzzer/sanitizer signal handlers; the harness calls `disable_crash_handler()`.
- The engine expects an orderly `Main::cleanup()`. Since the process only ever
  fuzzes, the harness registers a late `atexit()` that `_exit()`s, skipping the
  static-destructor teardown of the global `StringName`/`ClassDB` tables (which
  otherwise aborts at exit and is misattributed to the last input). Crashes
  during `LLVMFuzzerTestOneInput` are unaffected — ASan catches those mid-run.

Each iteration also drops the `fuzz://input.fs` parser/script entries from
`FSCache` so a crash always reproduces from the single input that caused it.

## Findings

Both findings are filed as issues; see the repo issue tracker. Reproducers under
`repro/`.

### Tokenizer OOB read on deep tab indentation (heap-buffer-overflow)

A line indented with ≥`tab_size` (default 4) tabs followed by an identifier makes
the tokenizer read before the start of the source buffer (`make_token()` →
`String::utf32(Span(_start, _current - _start))` with `_start < _source`). Root
cause is `_advance()` re-entering `check_indent()` at EOF mid-token combined with
`check_indent()`'s per-tab `column` inflation. Minimal trigger: `printf '\t\t\t\tt'`.
Reproducer: `repro/deep_tab_indent_oob.fs`. Memory-safety bug on tiny, ordinary
input — found by the analyzer-enabled campaign.

### Unbounded parser recursion → stack overflow (crash)

Deeply nested expressions overflow the native stack and crash the process with
`SIGSEGV` (`EXC_BAD_ACCESS` writing into the stack guard region) instead of
emitting a parse error. The recursive-descent expression parser has **no
recursion-depth limit** (`parse_precedence` → `parse_grouping` / `parse_array` /
`parse_dictionary` / unary → `parse_expression` → `parse_precedence`).

Reproduces with grouping `(`, subscript `[`, dictionary `{`, and unary `-`.
Threshold is ~20k–30k nesting levels (ASan ~3x stack usage; the limit is higher
in a non-ASan build but still finite and reachable).

```sh
python3 -c "open('x.fs','w').write('var x = ' + '('*40000 + '1' + ')'*40000)"
./bin/godot.macos.editor.dev.arm64.san.fuzz -runs=1 x.fs   # exit 139 (SIGSEGV)
```

Reproducer: `repro/deeply_nested_parens.fs`.

Impact: opening or completing a crafted/pathological `.fs` file crashes the
editor and the language server. Likely shared with upstream Godot. A fix would
track nesting depth in the parser and surface a normal parse error past a limit.
