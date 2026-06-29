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

## Testing Guidelines

Add or update tests with behavior changes. C++ tests use doctest macros from `tests/test_macros.h` and are included through `tests/test_main.cpp`. New C++ test skeletons can be created with `python tests/create_test.py Name path`, where `path` is relative to `tests/`. Foundry Script integration, completion, LSP, and refactor fixtures belong under `modules/foundry_script/tests/scripts/`; pair `.fs` fixtures with expected-output config files where the local test runner expects them.

## Commit & Pull Request Guidelines

Keep commits focused and readable. Recent history uses concise imperative subjects, sometimes with scope prefixes, for example `Polish strict argument diagnostics` and `docs(README): Add note about experimental status of this fork`. Prefer first lines under 72 characters. PRs should target `develop`, describe the behavior change, include relevant tests, link issues when applicable, and add screenshots or reproduction projects for editor-facing changes.

## Cursor Cloud specific instructions

This is a Godot Engine fork; the only product is the single `godot` binary (editor, runtime, headless tool, and unit-test runner in one). Build it with SCons on `platform=linuxbsd`.

- SCons is installed via `pip --user`, so its console script lives in `~/.local/bin` (added to PATH in `~/.bashrc`). Invoke as `scons` in a login shell, or robustly as `python3 -m SCons` in non-login shells.
- Build (mirrors CI flags), from repo root: `python3 -m SCons platform=linuxbsd target=editor dev_build=yes tests=yes module_text_server_fb_enabled=yes -j$(nproc)`. A clean build takes ~13 min on this VM; incremental rebuilds are much faster, so do NOT clean unless necessary.
  - CI additionally uses `dev_mode=yes` (warnings-as-errors). Prefer `dev_build=yes` for local iteration; use `dev_mode=yes` only when you need to reproduce CI warning failures.
- Output binary: `bin/godot.linuxbsd.editor.dev.x86_64`.
- Run the full C++ + Foundry Script test suite: `./bin/godot.linuxbsd.editor.dev.x86_64 --headless --test --force-colors`. Always pass `--headless`.
- Regenerate Foundry Script `.out` fixtures after intentional behavior changes: `./bin/godot.linuxbsd.editor.dev.x86_64 --headless --gdscript-generate-tests modules/foundry_script/tests/scripts`.
- The editor GUI does launch on the desktop (display `:1`), but the VM has no GPU: Vulkan init prints `VK_KHR_surface not found` errors and Godot falls back to software rendering. These errors are expected and non-blocking. For scripted/automated runs, prefer `--headless`.
- Lint/format gate (optional, not engine validation): `pre-commit run --all-files`.
