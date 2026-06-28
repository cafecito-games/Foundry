# GDScript parser fuzzing (libFuzzer prototype)

A coverage-guided libFuzzer harness that drives `GDScriptParser::parse()` over
mutated input. The parser's hard contract is that *any* byte sequence yields
either a clean error list or a valid tree — never a crash. The editor and LSP
parse untrusted/partial `.gd` buffers, so robustness here is a real concern.

## Building (macOS)

The harness replaces the platform `main()` with a libFuzzer entry point when
`use_fuzzer=yes` is passed. Apple's clang does not ship the libFuzzer runtime,
so the engine is compiled with Apple clang and linked against the runtime from a
Homebrew LLVM of a matching major version (`brew install llvm`).

```sh
scons platform=macos target=editor dev_build=yes use_fuzzer=yes use_asan=yes -j$(sysctl -n hw.ncpu)
```

`target=editor` is required: export-template builds exit at startup without a
`.pck`, while the editor (tool) build boots without a project.

## Running

```sh
# Seed corpus from the existing script fixtures, plus a keyword dictionary.
mkdir -p corpus && find ../scripts -name '*.gd' -exec cp {} corpus/ \;

ASAN_OPTIONS=detect_leaks=0 \
  ../../../../bin/godot.macos.editor.dev.arm64.san.fuzz \
  -dict=gdscript.dict -timeout=25 -rss_limit_mb=4096 \
  -jobs=6 -workers=6 corpus
```

Observed throughput on an M-series laptop: ~310 exec/s/worker, ~3.5M coverage
counters, ~6.5k edges reached over the parser/tokenizer from the seed corpus.

## Harness notes

The harness (`platform/macos/fuzz_gdscript_macos.mm`) boots the engine once via
`Main::setup(..., /*second_phase=*/true)` using `OS_MacOS_Headless` (the NSApp OS
runs a Cocoa loop that never returns). Two interactions with the engine are
worth knowing for any future port:

- Godot installs its own crash handler during setup, which shadows the
  libFuzzer/ASan signal handlers; the harness calls `disable_crash_handler()`.
- The engine expects an orderly `Main::cleanup()`. Since the process only ever
  fuzzes, the harness registers a late `atexit()` that `_exit()`s, skipping the
  static-destructor teardown of the global `StringName`/`ClassDB` tables (which
  otherwise aborts at exit and is misattributed to the last input). Crashes
  during `LLVMFuzzerTestOneInput` are unaffected — ASan catches those mid-run.

## Findings

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
python3 -c "open('x.gd','w').write('var x = ' + '('*40000 + '1' + ')'*40000)"
./bin/godot.macos.editor.dev.arm64.san.fuzz -runs=1 x.gd   # exit 139 (SIGSEGV)
```

Reproducer: `repro/deeply_nested_parens.gd`.

Impact: opening or completing a crafted/pathological `.gd` file crashes the
editor and the language server. Likely shared with upstream Godot. A fix would
track nesting depth in the parser and surface a normal parse error past a limit.
