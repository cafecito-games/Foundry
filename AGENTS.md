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
- Start the combined editor tooling host (language server plus debug adapter) for an IDE integration: `./bin/foundry.* tooling serve --project <project> --lsp-port 0 --dap-port 0`. It always runs headless and loopback-only, and prints one `FOUNDRY_TOOLING {...}` readiness line with the actually bound ports (or one `FOUNDRY_TOOLING_ERROR {...}` line and a nonzero exit when either listener cannot bind). `lsp serve` is a deprecated alias for the same host.

### Test run progress events

Full doctest runs can be quiet for long stretches while individual test cases execute. Normal stdout/stderr also mixes expected negative-test diagnostics, engine warnings, leak reports, and the final doctest summary, which makes it hard for agents to tell whether a run is healthy or hung.

`foundry test run` supports an opt-in progress stream for monitoring and automation:

- `--progress`: compact human-readable lines on stdout (`[foundry-test] START 42/2779 ...`).
- `--progress-format=text|jsonl`: choose the event encoding (`--progress` defaults to text).
- `--progress-file <path>`: write newline-delimited JSON events to a separate file. **Prefer this for agents** so progress can be tailed without scraping mixed engine/doctest output.
- `--progress-heartbeat-seconds <n>`: emit periodic `test_heartbeat` events while a test case is still running (default `30`; use `0` to disable heartbeats but keep start/end events).

Progress is off by default. When enabled, the runner emits structured events: `run_start`, `test_start`, `test_heartbeat`, `test_end`, and `run_end`. JSONL records include `version`, `event`, `index`, `test_count` (for the filtered set), test `name`/`suite`/`file`/`line`, `status`, `duration_ms`, and run-level counters on `run_end`.

Example agent-friendly usage:

```sh
./bin/foundry.* --headless test run --progress-format=jsonl --progress-file /tmp/foundry-test-progress.jsonl --force-colors
./bin/foundry.* --headless test run --progress --case "*FoundryCLI*" --force-colors
./bin/foundry.* --headless test run --progress-format=jsonl --progress-file /tmp/progress.jsonl --progress-heartbeat-seconds 30 --case "*Editor*" --force-colors
```

When monitoring a long run:

- Tail `--progress-file` for the current test identity and heartbeat timestamps instead of guessing from silence.
- Use `test_start`/`test_heartbeat` to detect stalls on a specific case before the suite finishes.
- Use `test_end`/`run_end` for structured pass/fail/skip counts without parsing doctest's mixed console output.
- `--quiet` still suppresses stdout progress, but `--progress-file` continues to emit events.

Do not scrape doctest console output for progress when `--progress-file` is available.


The Foundry editor embeds a local MCP (Model Context Protocol) server that lets agents drive the real editor GUI — clicking buttons, filling dialogs, editing inspector properties, running scenes — without screenshots or pixel coordinates. Use it whenever a task needs to operate or verify the editor UI itself (reproducing editor bugs, testing editor-facing features, exercising dialogs and docks end-to-end).

### Starting and connecting

- Launch the editor with automation enabled: `DISPLAY=:1 ./bin/foundry.* editor open --project <project> --automation` (optionally `--automation-port <port>` and `--automation-token <token>`; the transport defaults to `mcp`).
- On startup the editor prints a machine-readable line to stdout: `FOUNDRY_AUTOMATION {"transport":"mcp","endpoint":"http://127.0.0.1:<port>/mcp","token":"...","local_only":true}`. Parse it for the endpoint and bearer token.
- The transport is POST-only JSON-RPC 2.0 over HTTP. Every request needs `Authorization: Bearer <token>` and `Content-Type: application/json`. Start with an `initialize` request, then send `notifications/initialized`, then use `tools/call` and `resources/read`. There are no server-push notifications; poll `poll_events` for editor errors/warnings.

### Agent tool bridge

Prefer the stdio MCP bridge in `scripts/foundry_mcp_server.py` when an agent
should use editor automation as normal tool calls. Register it once in Claude,
Codex, Cursor, or another MCP-capable client, then use bridge tools such as
`foundry_launch_editor`, `foundry_connect`, `foundry_observe_ui`,
`foundry_find_elements`, `foundry_act`, and `foundry_wait_for`.

Example Claude Code registration from the repo root:

```sh
claude mcp add foundry-editor -- python3 scripts/foundry_mcp_server.py
```

Example bridge workflow after registration:

1. Call `foundry_launch_editor` with `project`, or call `foundry_connect` with
   an existing automation `endpoint` and `token`. Include `binary` only when the
   built `foundry` executable is not discoverable under `bin/foundry.*` in this
   checkout or the parent checkout for `.worktrees/<name>` branches.
2. Use `foundry_observe_ui` / `foundry_find_elements` to inspect the UI.
3. Use `foundry_act` with a semantic selector.
4. Use `foundry_wait_for` and `foundry_poll_events` instead of sleeping.
5. Use `foundry_disconnect` when the workflow is complete.

Use the lower-level Python client in `scripts/foundry_mcp/` only for checked-in
scripts and tests that need direct process control. Do not write ad hoc `urllib`
MCP clients in agent scratch files. For Claude, Codex, and Cursor configuration
examples, see `docs/editor_automation_mcp_client.md`.

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

### Remote visual review gallery

Whenever the work is substantial editor UI work — new or reworked docks, dialogs, inspectors, HUDs, themes, or any change whose correctness is judged *visually* — do not ask the reviewer to trust a prose description. Build a review gallery so the reviewer can validate the change is correct by looking at it, without a local build.

Use the stdlib-only helper `scripts/review_gallery.py` (see `scripts/review_gallery.README.md`). Capture editor screenshots (via the automation `capture_screenshot` tool) and add them to captioned boards, then serve them at one URL the reviewer can open from anywhere:

- Choose the board mode that makes the change *checkable*, not just visible:
  - `proof` — a compact block that demonstrates the change works (e.g. the dock still renders after the fix).
  - `design` — before/after pairs (`--pair before|after`) so an aesthetic or layout change can be judged against the prior state.
  - `walkthrough` — an ordered, step-captioned storyboard (open → act → observe) so a behavioral change can be followed and verified step by step.
- Caption every shot with what the reviewer should confirm ("dock stays docked after closing the last tab"), not just what it is. The gallery is only useful if each shot lets the reviewer decide *correct / not correct* on their own.
- Serving is local-only by default; `python3 scripts/review_gallery.py serve --public` brings up an ngrok tunnel and prints a shareable URL for a remote reviewer (falling back to a local URL if ngrok is unavailable). Add `--basic-auth user:pass` whenever you use `--public`, since the tunnel URL is otherwise reachable by anyone who has it.

The gallery ingests finished PNGs and is capture-source-agnostic, so it works today with any screenshot and improves automatically as on-demand capture evolves. Prefer producing a gallery over a wall of text for any UI-heavy change, and include the gallery URL in your summary or PR.

## Coding Style & Naming Conventions

Follow `.editorconfig`: UTF-8, LF line endings, final newline, 120-column limit, and trimmed trailing whitespace. C/C++ and most engine files use tabs with width 4; Python, `SConstruct`, and `SCsub` use 4 spaces; YAML and clang config files use 2 spaces. C++ formatting is enforced by `.clang-format`; Python/SCons formatting and imports are handled by Ruff, with mypy checks for Python. Keep filenames and APIs consistent with nearby Godot conventions, such as `snake_case` file names and test headers named `test_<area>.h`. Naming and content rules for Python test/check files live in `### Test authoring rules` below.

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

Tests that generate, mutate, or persist local files must write those files under the shared test scratch space instead of the repository root or tracked fixture directories. Agent-run tests get `FOUNDRY_TEST_SCRATCH=$REPO_ROOT/.test_scratch` from `scripts/agent_build.py`; C++ tests should use the Foundry test scratch helpers so aborted runs do not pollute `git status`. Use fixture directories only for intentional checked-in inputs and expected outputs, and keep fixture regeneration commands (`test generate-fixtures`, `test generate-format-fixtures`) as the explicit path for updating tracked files.

### Test authoring rules

A test asserts on observable behavior — a return value, an exit code, emitted output, a produced artifact, a parsed data structure. A test that asserts on the source text of another file is not a test. This is scaffolding hygiene, not blame: scaffolding a text-matching check while building a feature is fine, but it must be deleted in the same change that lands real coverage, not shipped as a permanent gate. See epic #1344.

- **Never** assert that a source file contains a substring to verify behavior implemented in that file. Write a C++ doctest, a Foundry Script fixture, or a Python unit test that executes the code instead.
- **Never** slice a source file on a function signature to grep inside it (`source.split("void Foo::bar() {")[1]`). Write a test that calls the function, or a check that parses a real AST/symbol table.
- **Never** assert on YAML or Markdown formatting: indentation, key order, whitespace, or `str.find()` positions. Parse workflow YAML with `yaml.safe_load` and assert on the graph via `.github/scripts/workflow_graph.py` instead.
- **Never** pin the body of an embedded script as a string constant. Extract the script to a real file and execute it in the test.
- **Never** assert that a Markdown document contains a particular phrase; documentation correctness is a review concern, not a test.
- **Never** assert that another test exists (`assertTrue(hasattr(SomeTests, "test_..."))`). Delete the assertion, or write the missing test.
- File-content policy worth enforcing — required or forbidden literal tokens in given paths — goes in `misc/checks/file_policy.toml` as data, never as new Python.
- Naming: `test_*.py` executes code and asserts on results; `check_*.py` under `misc/checks/` inspects files or artifacts, reports every violation, and exits non-zero. A `test_*.py` with no assertions is misnamed.
- Python tests belong in `tests/python_build/` (build-system behavior), `.github/scripts/tests/` (CI helper modules), `scripts/tests/` (developer tooling), or `tools/*/tests/` (standalone tools).
- Every new `pre-commit` hook goes below the marker at `.pre-commit-config.yaml:199` and must justify its `files:` regex; prefer extending an existing hook over adding one.

Self-check before opening a PR that touches these areas (non-empty output means work remains — see the tracking cleanup issues under epic #1344):

```sh
# No source-signature slicing.
git grep -nE '\.split\("(void|bool|String|private fun|const val|def )' -- '*.py'
# No assertions on documentation prose.
git grep -n "docs/superpowers" -- '*/test_*.py' '*/check_*.py'
# No assertions that other tests exist.
git grep -n "hasattr(" -- '*/test_*.py'
# No test_* file without assertions, and no check_* file with them.
git grep -l "assert" -- 'misc/checks/check_*.py'
```

## Commit & Pull Request Guidelines

Keep commits focused and readable. Recent history uses concise imperative subjects, sometimes with scope prefixes, for example `Polish strict argument diagnostics` and `docs(README): Add note about experimental status of this fork`. Prefer first lines under 72 characters. PRs should target `develop`, describe the behavior change, include relevant tests, link issues when applicable, and add screenshots or reproduction projects for editor-facing changes.

## Cursor Cloud specific instructions

This is a Godot Engine fork; the only product is the single `foundry` binary (editor, runtime, headless tool, and unit-test runner in one). Build it with SCons on `platform=linuxbsd`.

- Prefer the agent-friendly wrappers for cloud work: `python3 scripts/agent_build.py`, `python3 scripts/agent_build.py --test --case "*FoundryCLI*"`, and `python3 scripts/agent_debug.py --case "*FoundryCLI*"`. They keep command lines stable, stream progress, and write `/tmp/foundry-build.log`. The build wrapper uses CI-style `dev_mode=yes` by default so warnings are treated as errors before PR checks. Use `--dev-build` only as a temporary fast-iteration shortcut, and rerun the wrapper without `--dev-build` before marking work ready, pushing, or opening/updating a PR.
  - `scripts/agent_build.py` writes JSONL command progress to `/tmp/foundry-build-progress.jsonl` by default. Tail this file for build movement instead of scraping mixed SCons output: it emits `command_start`, `command_output`, `command_heartbeat`, and `command_end` events with a `phase` (`build` or `test`), elapsed time, output lines, and final status.
  - Use `--progress-file <path>` to choose a different JSONL file, `--no-progress-file` to disable the file, and `--append-progress` to append instead of replacing it. Use `--progress-format=jsonl` only when the supervising tool needs JSONL on stdout; in that mode human-readable build output is routed to stderr so stdout remains parseable.
- SCons is installed via `pip --user`, so its console script lives in `~/.local/bin` (added to PATH in `~/.bashrc`). Invoke as `scons` in a login shell, robustly as `python3 -m SCons` in non-login shells, or through `python3 scripts/agent_build.py` when an agent needs progress reporting.
- Build (mirrors CI flags), from repo root: `python3 -m SCons platform=linuxbsd target=editor dev_mode=yes dev_build=yes tests=yes module_text_server_fb_enabled=yes cache_path="$HOME/.scons_cache" -j$(nproc)`. A clean build takes ~13-15 min on this VM; incremental rebuilds are much faster, so do NOT clean unless necessary.
  - `dev_mode=yes` enables stricter checks and warnings-as-errors. Use `dev_build=yes` without `dev_mode=yes` only for temporary local iteration, never as the final validation before a PR.
  - SCons build cache: always pass `cache_path="$HOME/.scons_cache"`. The cache lives in `$HOME` (NOT the repo tree) on purpose, so it is captured by the Cloud VM snapshot and survives whatever git refresh runs on a fresh agent. It is pre-populated, so even a full `--clean` rebuild on a new agent retrieves objects from cache and finishes in ~1.5 min instead of ~15 min. Keep the same build flags: changing flags (e.g. `dev_mode`, target) produces different object hashes and misses the cache. The cache is content-addressed and self-maintaining; do not delete `$HOME/.scons_cache`.
- Output binary: `bin/foundry.linuxbsd.editor.dev.x86_64` (this fork renames the binary from `godot` to `foundry`).
- Run the full C++ + Foundry Script test suite: `DISPLAY=:1 ./bin/foundry.linuxbsd.editor.dev.x86_64 --headless test run --force-colors`. For long full-suite runs, add `--progress-format=jsonl --progress-file /tmp/foundry-test-progress.jsonl` (and optionally `--progress-heartbeat-seconds 30`) so you can tail structured progress instead of inferring health from silence. Always pass `--headless` in scripted/CI-style runs. The run prints `ObjectDB instances leaked`/`resources still in use at exit` and may exit non-zero at cleanup even when every test passes; trust the `[doctest] Status: SUCCESS!` summary line.
- Prefix the full-suite run with `DISPLAY=:1` (as shown above). Some tests, such as the editor automation MVP acceptance workflow, launch a real editor GUI subprocess and self-skip when no display is available. Without `DISPLAY=:1` those tests silently skip instead of running, so a green `--headless`-only run can hide GUI-dependent failures. Keep `--headless` for the tool's own render mode; `DISPLAY=:1` only provides the X display those subprocesses need.
- Run a focused doctest filter: `./bin/foundry.linuxbsd.editor.dev.x86_64 --headless test run --case "*FoundryCLI*" --force-colors`.
- Debug a focused doctest filter with GDB: `python3 scripts/agent_debug.py --case "*FoundryCLI*"`. Use `--build-first` to compile before launching GDB, and `--batch` to run non-interactively and print backtraces. The cloud image should provide `gdb`; if it is missing, install it before debugging.
- Regenerate Foundry Script `.out` fixtures after intentional behavior changes: `./bin/foundry.linuxbsd.editor.dev.x86_64 --headless test generate-fixtures modules/foundry_script/tests/scripts`.
- Regenerate formatter fixtures after intentional formatting changes: `./bin/foundry.linuxbsd.editor.dev.x86_64 --headless test generate-format-fixtures modules/foundry_script/tests/scripts/format`.
- Foundry Script files use the `.fs` extension, and a project's config file is `project.foundry` (NOT `project.godot`); editor commands will not recognize a project that only has `project.godot`.
- The editor GUI does launch on the desktop (display `:1`) via `DISPLAY=:1 ./bin/foundry.linuxbsd.editor.dev.x86_64 editor open --project <project>`, but the VM has no GPU: Vulkan init prints `VK_KHR_surface not found` errors and rendering falls back to OpenGL/llvmpipe software rendering. These errors are expected and non-blocking. For scripted/automated runs, prefer `--headless`.
- Lint/format gate (optional, not engine validation): `pre-commit run --all-files`.

### Android development/testing

The Android SDK + NDK + JDK 17 are preinstalled and captured by the VM snapshot (like `$HOME/.scons_cache`), so a fresh agent already has them; they are NOT reinstalled by the update script. Login shells (`bash -l`) export `ANDROID_HOME`, `ANDROID_SDK_ROOT`, `JAVA_HOME`, and PATH via `~/.bashrc` (`FOUNDRY_ANDROID_ENV` block). Non-login shells must set these first (e.g. `source ~/.bashrc`) or Gradle/SCons Android builds won't find the toolchain.

- Locations: SDK at `$HOME/Android/Sdk`; NDK at `$HOME/Android/Sdk/ndk/29.0.14206865`; JDK 17 at `/usr/lib/jvm/java-17-openjdk-amd64`. Note the system default JDK is 21; Android/Gradle require JDK 17, which is why `JAVA_HOME` is pinned to 17 in `~/.bashrc`.
- Pinned versions (authoritative in `platform/android/java/app/config.gradle` and `platform/android/detect.py`): NDK `29.0.14206865`, `compileSdk`/`targetSdk` 36, build-tools `36.1.0`, minSdk 24, JDK 17. SCons sets `ANDROID_NDK_ROOT` itself from `ANDROID_HOME`; do not set it manually.
- Before any Android SCons build, ensure Swappy sources exist (`swappy=yes` requires them): `python3 misc/scripts/install_swappy_android.py` populates `thirdparty/swappy-frame-pacing/` (gitignored). It's a one-time download; re-run only if that dir is missing.
- Build one native cell (proven working here, ~7 min for arm64 with the shared cache), from repo root in a login shell: `python3 -m SCons platform=android target=template_debug arch=arm64 production=no dev_mode=no dev_build=no debug_symbols=no tests=no swappy=yes module_text_server_fb_enabled=yes cache_path="$HOME/.scons_cache" -j$(nproc)`. Output lands in `bin/android-native/<git-rev>/debug/arm64-v8a/` (`libfoundry_android.so`, `libc++_shared.so`, `provenance.json`). `target=editor` is rejected on Android. Full templates need all 12 cells (3 build types x arm32/arm64/x86_32/x86_64), then `platform/android/java/gradlew generateFoundryTemplates`.
- Host-side Android contract tests (no device needed; these back the `Foundry Android *` pre-commit hooks) run with plain `unittest`, e.g. `python3 -m unittest tests.python_build.test_android_jni_contract tests.python_build.test_android_native_staging`. The JNI contract test needs `javap` (JDK 17 on PATH).
- Device/emulator acceptance (`tests.python_build.test_android_device_acceptance`, `android_device_acceptance.py`) needs an API 36 emulator/KVM, which this VM does not provide; those exercise `adb`/emulator flows and are out of scope for headless CPU-only runs.
- If the toolchain is ever missing on a fresh VM (snapshot lost it), reinstall with: `sdkmanager "cmdline-tools;latest" "platform-tools" "platforms;android-36" "build-tools;36.1.0" "ndk;29.0.14206865"` (cmdline-tools from `https://dl.google.com/android/repository/commandlinetools-linux-11076708_latest.zip` into `$HOME/Android/Sdk/cmdline-tools/latest`) and `apt-get install -y openjdk-17-jdk-headless`.
