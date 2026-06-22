# Repository Guidelines

## Project Structure & Module Organization

This repository is a CafecitoGames fork of Godot Engine with active work around stricter GDScript typing, editor scripting tools, and LSP/refactoring surfaces. Core engine code lives in `core/`, `scene/`, `servers/`, `drivers/`, `main/`, and `editor/`. Platform integrations are under `platform/`, optional engine features under `modules/`, and vendored dependencies under `thirdparty/`. GDScript implementation and editor tooling are concentrated in `modules/gdscript/`, with language server code in `modules/gdscript/language_server/` and script-based tests in `modules/gdscript/tests/scripts/`. C++ unit tests live in `tests/`; class reference XML lives in `doc/classes/` and `*/doc_classes/`.

## Build, Test, and Development Commands

- `python -m pip install scons pre-commit`: install the usual local build and hook tooling.
- `scons platform=macos target=editor dev_build=yes tests=yes`: build a macOS editor binary with development checks and unit tests enabled. Use `platform=linuxbsd` on Linux.
- `scons platform=macos target=editor dev_mode=yes tests=yes`: closer to CI defaults; enables extra warnings, strict checks, and warnings-as-errors.
- `./bin/godot.* --test --force-colors`: run compiled C++ tests. Use `--headless` for CI/Linux displayless runs.
- `pre-commit run --all-files`: run formatting, linting, spelling, XML/doc checks, and generated-doc dry runs configured in `.pre-commit-config.yaml`.

## Coding Style & Naming Conventions

Follow `.editorconfig`: UTF-8, LF line endings, final newline, 120-column limit, and trimmed trailing whitespace. C/C++ and most engine files use tabs with width 4; Python, `SConstruct`, and `SCsub` use 4 spaces; YAML and clang config files use 2 spaces. C++ formatting is enforced by `.clang-format`; Python/SCons formatting and imports are handled by Ruff, with mypy checks for Python. Keep filenames and APIs consistent with nearby Godot conventions, such as `snake_case` file names and test headers named `test_<area>.h`.

## Testing Guidelines

Add or update tests with behavior changes. C++ tests use doctest macros from `tests/test_macros.h` and are included through `tests/test_main.cpp`. New C++ test skeletons can be created with `python tests/create_test.py Name path`, where `path` is relative to `tests/`. GDScript integration, completion, LSP, and refactor fixtures belong under `modules/gdscript/tests/scripts/`; pair `.gd` fixtures with expected-output config files where the local test runner expects them.

## Commit & Pull Request Guidelines

Keep commits focused and readable. Recent history uses concise imperative subjects, sometimes with scope prefixes, for example `Polish strict argument diagnostics` and `docs(README): Add note about experimental status of this fork`. Prefer first lines under 72 characters. PRs should target `master`, describe the behavior change, include relevant tests, link issues when applicable, and add screenshots or reproduction projects for editor-facing changes.

