# Repository Guidelines

## Project Structure & Module Organization

This repository is a CafecitoGames fork of Godot Engine with active work around stricter Foundry Script typing, editor scripting tools, and LSP/refactoring surfaces. Core engine code lives in `core/`, `scene/`, `servers/`, `drivers/`, `main/`, and `editor/`. Platform integrations are under `platform/`, optional engine features under `modules/`, and vendored dependencies under `thirdparty/`. Foundry Script implementation and editor tooling are concentrated in `modules/foundry_script/`, with language server code in `modules/foundry_script/language_server/` and script-based tests in `modules/foundry_script/tests/scripts/`. C++ unit tests live in `tests/`; class reference XML lives in `doc/classes/` and `*/doc_classes/`.

## Build, Test, and Development Commands

- `python -m pip install scons pre-commit`: install the usual local build and hook tooling.
- `scons platform=macos target=editor dev_build=yes tests=yes`: build a macOS editor binary with development checks and unit tests enabled. Use `platform=linuxbsd` on Linux.
- `scons platform=macos target=editor dev_mode=yes tests=yes`: closer to CI defaults; enables extra warnings, strict checks, and warnings-as-errors.
- `./bin/foundry.* --headless test run --force-colors`: run the compiled C++ and Foundry Script test suites with the supported command-first CLI.
- `pre-commit run --all-files`: run formatting, linting, spelling, XML/doc checks, and generated-doc dry runs configured in `.pre-commit-config.yaml`.

## Foundry CLI Usage

Use the supported command-first CLI: `foundry <command> <subcommand> [options]`. Do not use deprecated legacy invocations in agent instructions, scripts, or verification commands: `--test`, `--path`, `--editor`, `--project-manager`, `--import`, `--foundry_script-generate-tests`, or `--foundry_script-generate-format-tests`. Prefer `--case <pattern>` on `test run` instead of raw doctest `--test-case=...` filters. Use `--project <dir>` for project paths.

- Help is hierarchical: `./bin/foundry.* --help`, `./bin/foundry.* test --help`, `./bin/foundry.* test run --help`, or `./bin/foundry.* --json --help`.
- Full engine and Foundry Script tests: `./bin/foundry.* --headless test run --force-colors`.
- Scoped doctest run: `./bin/foundry.* --headless test run --case "*FoundryCLI*" --force-colors`.
- Regenerate Foundry Script `.out` fixtures: `./bin/foundry.* --headless test generate-fixtures modules/foundry_script/tests/scripts`.
- Regenerate formatter `expected.fs` fixtures: `./bin/foundry.* --headless test generate-format-fixtures modules/foundry_script/tests/scripts/format`.
- Run a project test runner script: `./bin/foundry.* --headless project test --project <project> --runner res://path/to/runner.fs -- <runner args>`.
- Open the editor GUI: `DISPLAY=:1 ./bin/foundry.* editor open --project <project>`.

## Editor MCP Automation

The Foundry editor embeds a local MCP (Model Context Protocol) server that lets agents drive the real editor GUI — clicking buttons, filling dialogs, editing inspector properties, running scenes — without screenshots or pixel coordinates. Use it whenever a task needs to operate or verify the editor UI itself (reproducing editor bugs, testing editor-facing features, exercising dialogs and docks end-to-end).

### Starting and connecting

- Launch the editor with automation enabled: `DISPLAY=:1 ./bin/foundry.* editor open --project <project> --automation` (optionally `--automation-port <port>` and `--automation-token <token>`; the transport defaults to `mcp`).
- On startup the editor prints a machine-readable line to stdout: `FOUNDRY_AUTOMATION {"transport":"mcp","endpoint":"http://127.0.0.1:<port>/mcp","token":"...","local_only":true}`. Parse it for the endpoint and bearer token.
- The transport is POST-only JSON-RPC 2.0 over HTTP. Every request needs `Authorization: Bearer <token>` and `Content-Type: application/json`. Start with an `initialize` request, then send `notifications/initialized`, then use `tools/call` and `resources/read`. There are no server-push notifications; poll `poll_events` for editor errors/warnings.

### Tools and typical loop

`tools/list` returns full input/output schemas. The core loop is observe → select → act → wait:

- `observe_ui`: semantic UI tree (windows, roles, names, actions, focus, modal stack). Large trees paginate via `children_next_cursor`; fetch element subtrees with the `foundry://ui/subtree/{id}` resource.
- `find_elements`: resolve a semantic selector (`role`, `name`, `text_contains`, `within`, ...) to element summaries.
- `act`: perform `click`, `set_text`, `type_text`, `select`, `choose_menu_item`, `set_value`, ... on a selected element, optionally combined with a `wait` condition in one call.
- `wait_for`: wait for conditions like `selector_appears`, `modal_stack_settled`, `import_reload_idle`, or `log_contains` instead of sleeping.
- `read_editor_state`, `read_editor_log`, `run_command`, `list_commands`, `poll_events`: state readback, log readback, and command-palette execution.

Default snapshots hide internal implementation children of controls (a `SpinBox`'s embedded `LineEdit`, `Tree`/`ItemList` scrollbars) but always include `Window`/dialog internals such as OK/Cancel buttons. Pass `include_internal: true` to `observe_ui`/`find_elements`/`act` to inspect or drive internals; such elements are flagged `"internal": true` and should not be depended on by default. See the internal-child policy in `docs/superpowers/specs/2026-07-03-editor-agent-automation-design.md`.

### Reasonable use cases

- Reproducing and verifying editor bug fixes through the same GUI path a user takes (open a dialog, click Create, assert the scene tree changed).
- End-to-end testing of editor features: create nodes via the Scene dock, edit inspector properties, save scenes, run a scene and read errors from the log.
- Debugging UI state: dump the semantic tree of a misbehaving dock or dialog, check focus/modal state, or inspect a control's internals with `include_internal`.
- Driving reproduction projects attached to issues: script the exact click/type sequence and capture structured diagnostics on failure (`attach_screenshot_on_failure`).

### Improving the capability

If the MCP surface is not enough to complete a task — a control has no stable role/name, an action or wait condition is missing, internal children you need are not exposed, or results are too large/noisy — do not fall back to brittle workarounds silently. Prefer improving the automation layer itself (`editor/automation/`): add the missing role/action/metadata, selector field, wait condition, or snapshot option, with tests, as part of your change or as a proposed follow-up. At minimum, report the concrete gap in your summary so the capability keeps improving.

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

- Prefer the agent-friendly wrappers for cloud work: `python3 scripts/agent_build.py`, `python3 scripts/agent_build.py --test --case "*FoundryCLI*"`, and `python3 scripts/agent_debug.py --case "*FoundryCLI*"`. They keep command lines stable, stream progress, and write `/tmp/foundry-build.log`. The build wrapper uses CI-style `dev_mode=yes` by default so warnings are treated as errors before PR checks. Use `--dev-build` only as a temporary fast-iteration shortcut, and rerun the wrapper without `--dev-build` before marking work ready, pushing, or opening/updating a PR.
- SCons is installed via `pip --user`, so its console script lives in `~/.local/bin` (added to PATH in `~/.bashrc`). Invoke as `scons` in a login shell, robustly as `python3 -m SCons` in non-login shells, or through `python3 scripts/agent_build.py` when an agent needs progress reporting.
- Build (mirrors CI flags), from repo root: `python3 -m SCons platform=linuxbsd target=editor dev_mode=yes dev_build=yes tests=yes module_text_server_fb_enabled=yes cache_path="$HOME/.scons_cache" -j$(nproc)`. A clean build takes ~13-15 min on this VM; incremental rebuilds are much faster, so do NOT clean unless necessary.
  - `dev_mode=yes` enables stricter checks and warnings-as-errors. Use `dev_build=yes` without `dev_mode=yes` only for temporary local iteration, never as the final validation before a PR.
  - SCons build cache: always pass `cache_path="$HOME/.scons_cache"`. The cache lives in `$HOME` (NOT the repo tree) on purpose, so it is captured by the Cloud VM snapshot and survives whatever git refresh runs on a fresh agent. It is pre-populated, so even a full `--clean` rebuild on a new agent retrieves objects from cache and finishes in ~1.5 min instead of ~15 min. Keep the same build flags: changing flags (e.g. `dev_mode`, target) produces different object hashes and misses the cache. The cache is content-addressed and self-maintaining; do not delete `$HOME/.scons_cache`.
- Output binary: `bin/foundry.linuxbsd.editor.dev.x86_64` (this fork renames the binary from `godot` to `foundry`).
- Run the full C++ + Foundry Script test suite: `DISPLAY=:1 ./bin/foundry.linuxbsd.editor.dev.x86_64 --headless test run --force-colors`. Always pass `--headless` in scripted/CI-style runs. The run prints `ObjectDB instances leaked`/`resources still in use at exit` and may exit non-zero at cleanup even when every test passes; trust the `[doctest] Status: SUCCESS!` summary line.
- Prefix the full-suite run with `DISPLAY=:1` (as shown above). Some tests, such as the editor automation MVP acceptance workflow, launch a real editor GUI subprocess and self-skip when no display is available. Without `DISPLAY=:1` those tests silently skip instead of running, so a green `--headless`-only run can hide GUI-dependent failures. Keep `--headless` for the tool's own render mode; `DISPLAY=:1` only provides the X display those subprocesses need.
- Run a focused doctest filter: `./bin/foundry.linuxbsd.editor.dev.x86_64 --headless test run --case "*FoundryCLI*" --force-colors`.
- Debug a focused doctest filter with GDB: `python3 scripts/agent_debug.py --case "*FoundryCLI*"`. Use `--build-first` to compile before launching GDB, and `--batch` to run non-interactively and print backtraces. The cloud image should provide `gdb`; if it is missing, install it before debugging.
- Regenerate Foundry Script `.out` fixtures after intentional behavior changes: `./bin/foundry.linuxbsd.editor.dev.x86_64 --headless test generate-fixtures modules/foundry_script/tests/scripts`.
- Regenerate formatter fixtures after intentional formatting changes: `./bin/foundry.linuxbsd.editor.dev.x86_64 --headless test generate-format-fixtures modules/foundry_script/tests/scripts/format`.
- Foundry Script files use the `.fs` extension, and a project's config file is `project.foundry` (NOT `project.godot`); editor commands will not recognize a project that only has `project.godot`.
- The editor GUI does launch on the desktop (display `:1`) via `DISPLAY=:1 ./bin/foundry.linuxbsd.editor.dev.x86_64 editor open --project <project>`, but the VM has no GPU: Vulkan init prints `VK_KHR_surface not found` errors and rendering falls back to OpenGL/llvmpipe software rendering. These errors are expected and non-blocking. For scripted/automated runs, prefer `--headless`.
- Lint/format gate (optional, not engine validation): `pre-commit run --all-files`.
