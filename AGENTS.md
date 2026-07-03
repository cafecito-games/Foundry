# Repository Guidelines

## Project Structure & Module Organization

This repository is a CafecitoGames fork of Godot Engine with active work around stricter Foundry Script typing, editor scripting tools, and LSP/refactoring surfaces. Core engine code lives in `core/`, `scene/`, `servers/`, `drivers/`, `main/`, and `editor/`. Platform integrations are under `platform/`, optional engine features under `modules/`, and vendored dependencies under `thirdparty/`. Foundry Script implementation and editor tooling are concentrated in `modules/foundry_script/`, with language server code in `modules/foundry_script/language_server/` and script-based tests in `modules/foundry_script/tests/scripts/`. C++ unit tests live in `tests/`; class reference XML lives in `doc/classes/` and `*/doc_classes/`.

## Build, Test, and Development Commands

- `python -m pip install scons pre-commit`: install the usual local build and hook tooling.
- `scons platform=macos target=editor dev_build=yes tests=yes`: build a macOS editor binary with development checks and unit tests enabled. Use `platform=linuxbsd` on Linux.
- `scons platform=macos target=editor dev_mode=yes tests=yes`: closer to CI defaults; enables extra warnings, strict checks, and warnings-as-errors.
- `./bin/godot.* --test --force-colors`: run compiled C++ tests. Use `--headless` for CI/Linux displayless runs.
- `pre-commit run --all-files`: run formatting, linting, spelling, XML/doc checks, and generated-doc dry runs configured in `.pre-commit-config.yaml`.

## Coding Style & Naming Conventions

Follow `.editorconfig`: UTF-8, LF line endings, final newline, 120-column limit, and trimmed trailing whitespace. C/C++ and most engine files use tabs with width 4; Python, `SConstruct`, and `SCsub` use 4 spaces; YAML and clang config files use 2 spaces. C++ formatting is enforced by `.clang-format`; Python/SCons formatting and imports are handled by Ruff, with mypy checks for Python. Keep filenames and APIs consistent with nearby Godot conventions, such as `snake_case` file names and test headers named `test_<area>.h`.

## Grammar Specification

The Foundry Script grammar is documented exhaustively in `modules/foundry_script/GRAMMAR.md` (ISO 14977 EBNF plus the Pratt-parser precedence table). It is a normative spec used to re-implement the front-end (tokenizer + Pratt parser) in other languages. Any change to the scripting language that affects its grammar — adding/removing/renaming tokens or keywords, changing operator precedence or associativity, altering statement/declaration/type/expression/pattern syntax, or changing the built-in annotation set (typically edits to `modules/foundry_script/fs_tokenizer.{h,cpp}` or `modules/foundry_script/fs_parser.{h,cpp}`) — must be reflected in `modules/foundry_script/GRAMMAR.md` in the same change so it stays authoritative.

## Script-Extensible Native APIs

When adding a native class intended for Foundry Script users to extend, do not rely on C++ virtual
dispatch for script overrides. Bound native methods can be called from scripts, but native callers
holding a C++ pointer must use `Object::call()`/`callp()` through a centralized helper when invoking
user-overridable hooks.

For each script-extensible hook, document the method as script-dispatched near `_bind_methods()` and
in class docs, add it to `FSScriptExtensibleNativeHooks` so `NATIVE_METHOD_OVERRIDE` is not emitted
for intentional hooks, provide one native helper/invoker instead of scattering raw `call("hook")`
call sites, and add regression tests proving script subclasses can override the hook without analyzer
warnings and that native runtime code dispatches to the script method. Include coverage for native
subclasses of the extensible base and for unrelated native method overrides continuing to warn/error.

## Testing Guidelines

Add or update tests with behavior changes. C++ tests use doctest macros from `tests/test_macros.h` and are included through `tests/test_main.cpp`. New C++ test skeletons can be created with `python tests/create_test.py Name path`, where `path` is relative to `tests/`. Foundry Script integration, completion, LSP, and refactor fixtures belong under `modules/foundry_script/tests/scripts/`; pair `.fs` fixtures with expected-output config files where the local test runner expects them.

## Commit & Pull Request Guidelines

Keep commits focused and readable. Recent history uses concise imperative subjects, sometimes with scope prefixes, for example `Polish strict argument diagnostics` and `docs(README): Add note about experimental status of this fork`. Prefer first lines under 72 characters. PRs should target `develop`, describe the behavior change, include relevant tests, link issues when applicable, and add screenshots or reproduction projects for editor-facing changes.

## Cursor Cloud specific instructions

This is a Godot Engine fork; the only product is the single `foundry` binary (editor, runtime, headless tool, and unit-test runner in one). Build it with SCons on `platform=linuxbsd`.

- SCons is installed via `pip --user`, so its console script lives in `~/.local/bin` (added to PATH in `~/.bashrc`). Invoke as `scons` in a login shell, or robustly as `python3 -m SCons` in non-login shells.
- Build (mirrors CI flags), from repo root: `python3 -m SCons platform=linuxbsd target=editor dev_build=yes tests=yes module_text_server_fb_enabled=yes cache_path="$HOME/.scons_cache" -j$(nproc)`. A clean build takes ~13-15 min on this VM; incremental rebuilds are much faster, so do NOT clean unless necessary.
  - CI additionally uses `dev_mode=yes` (warnings-as-errors). Prefer `dev_build=yes` for local iteration; use `dev_mode=yes` only when you need to reproduce CI warning failures.
  - SCons build cache: always pass `cache_path="$HOME/.scons_cache"`. The cache lives in `$HOME` (NOT the repo tree) on purpose, so it is captured by the Cloud VM snapshot and survives whatever git refresh runs on a fresh agent. It is pre-populated, so even a full `--clean` rebuild on a new agent retrieves objects from cache and finishes in ~1.5 min instead of ~15 min. Keep the same build flags: changing flags (e.g. `dev_mode`, target) produces different object hashes and misses the cache. The cache is content-addressed and self-maintaining; do not delete `$HOME/.scons_cache`.
- Output binary: `bin/foundry.linuxbsd.editor.dev.x86_64` (this fork renames the binary from `godot` to `foundry`).
- Run the full C++ + Foundry Script test suite: `./bin/foundry.linuxbsd.editor.dev.x86_64 --headless --test --force-colors`. Always pass `--headless`. The run prints `ObjectDB instances leaked`/`resources still in use at exit` and may exit non-zero at cleanup even when every test passes; trust the `[doctest] Status: SUCCESS!` summary line.
- Regenerate Foundry Script `.out` fixtures after intentional behavior changes: `./bin/foundry.linuxbsd.editor.dev.x86_64 test generate-fixtures modules/foundry_script/tests/scripts`.
- Foundry Script files use the `.fs` extension, and a project's config file is `project.foundry` (NOT `project.godot`); the Project Manager and `--editor` will not recognize a project that only has `project.godot`.
- The editor GUI does launch on the desktop (display `:1`) via `DISPLAY=:1 ./bin/foundry.linuxbsd.editor.dev.x86_64 --path <project> --editor`, but the VM has no GPU: Vulkan init prints `VK_KHR_surface not found` errors and rendering falls back to OpenGL/llvmpipe software rendering. These errors are expected and non-blocking. For scripted/automated runs, prefer `--headless`.
- Lint/format gate (optional, not engine validation): `pre-commit run --all-files`.
