# Hierarchical CLI Help Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers-extended-cc:subagent-driven-development (recommended) or superpowers-extended-cc:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the ~300-line monolithic `foundry --help` dump with hierarchical help (nouns → verbs → options) driven by a declarative command registry, plus machine-readable JSON help.

**Architecture:** A new `main/cli_help.{h,cpp}` holds static registry data (nouns, commands, options) and renders text/JSON help from it. `FoundryCLIParser` learns to detect help flags and report the requested scope in `ParseResult`. `Main::setup()` dispatches scoped help before engine init and the legacy option sections in `print_help()` are deleted. Doctest cross-checks keep registry and parser from drifting.

**Tech Stack:** Godot core C++ (no STL, no exceptions — the build uses `-fno-exceptions`), SCons, doctest via `tests/test_macros.h`.

**Spec:** `docs/superpowers/specs/2026-07-02-cli-help-restructure-design.md`
**Follow-up (out of scope):** issue #830 — legacy flag migration and hard rejection.

**Build & test commands (macOS local):**

- Build: `scons platform=macos target=editor dev_build=yes tests=yes` (first build is slow; incremental builds are fast)
- Run scoped tests: `./bin/foundry.macos.editor.dev.arm64 --headless --test --test-case="*FoundryCLI*"` (adjust arch suffix if different; trust the `[doctest] Status: SUCCESS!` line)

**Native task mapping:** Task 1 → #8, Task 2 → #7, Task 3 → #9, Task 4 → #10, Task 5 → #11, Task 6 → #12.

---

### Task 1: Help detection in FoundryCLIParser

**Goal:** `ParseResult` reports help requests with the deepest command scope parsed so far; noun scope is recorded on errors too.

**Files:**
- Modify: `main/cli_parser.h:38-47` (ParseResult struct)
- Modify: `main/cli_parser.cpp`
- Test: `tests/core/os/test_foundry_cli_parser.h`

**Acceptance Criteria:**
- [ ] `-h`, `--help`, `/?` anywhere in a new-CLI invocation set `help_requested = true` with `ok = true` and the correct `command_path` scope
- [ ] `foundry help [noun [verb]]` alias sets `help_requested` with the given scope tokens
- [ ] Help requests skip required-option validation (e.g. `script migrate --help` does not fail on missing `--project`)
- [ ] Bare noun and unknown-verb errors record the noun in `command_path`
- [ ] All pre-existing parser tests still pass

**Verify:** `./bin/foundry.macos.editor.dev.arm64 --headless --test --test-case="*FoundryCLIParser*"` → `Status: SUCCESS!`

**Steps:**

- [ ] **Step 1: Write the failing tests**

Append to `tests/core/os/test_foundry_cli_parser.h` (before the closing `} // namespace TestFoundryCLIParser`):

```cpp
TEST_CASE("[FoundryCLIParser] Help flag at top level is detected") {
	FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(make_args({ "foundry", "--help" }));
	REQUIRE_MESSAGE(result.ok, result.error);
	CHECK(result.help_requested);
	CHECK(result.command_path.is_empty());
}

TEST_CASE("[FoundryCLIParser] Help flag scoped to a noun") {
	FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(make_args({ "foundry", "script", "--help" }));
	REQUIRE_MESSAGE(result.ok, result.error);
	CHECK(result.help_requested);
	CHECK_EQ(result.command_path, make_args({ "script" }));
}

TEST_CASE("[FoundryCLIParser] Help flag scoped to a command") {
	FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(make_args({ "foundry", "script", "format", "--help" }));
	REQUIRE_MESSAGE(result.ok, result.error);
	CHECK(result.help_requested);
	CHECK_EQ(result.command_path, make_args({ "script", "format" }));
}

TEST_CASE("[FoundryCLIParser] Help flag skips required option validation") {
	// script migrate requires --project, but a help request must not fail on it.
	FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(make_args({ "foundry", "script", "migrate", "--apply", "--help" }));
	REQUIRE_MESSAGE(result.ok, result.error);
	CHECK(result.help_requested);
	CHECK_EQ(result.command_path, make_args({ "script", "migrate" }));
}

TEST_CASE("[FoundryCLIParser] Short and DOS help flags are recognized") {
	for (const String &flag : { String("-h"), String("/?") }) {
		FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(make_args({ "foundry", "project", flag }));
		REQUIRE_MESSAGE(result.ok, result.error);
		CHECK(result.help_requested);
		CHECK_EQ(result.command_path, make_args({ "project" }));
	}
}

TEST_CASE("[FoundryCLIParser] Help alias routes scope") {
	FoundryCLIParser::ParseResult top = FoundryCLIParser::parse(make_args({ "foundry", "help" }));
	REQUIRE_MESSAGE(top.ok, top.error);
	CHECK(top.help_requested);
	CHECK(top.command_path.is_empty());

	FoundryCLIParser::ParseResult noun = FoundryCLIParser::parse(make_args({ "foundry", "help", "script" }));
	REQUIRE_MESSAGE(noun.ok, noun.error);
	CHECK(noun.help_requested);
	CHECK_EQ(noun.command_path, make_args({ "script" }));

	FoundryCLIParser::ParseResult verb = FoundryCLIParser::parse(make_args({ "foundry", "help", "script", "format" }));
	REQUIRE_MESSAGE(verb.ok, verb.error);
	CHECK(verb.help_requested);
	CHECK_EQ(verb.command_path, make_args({ "script", "format" }));
}

TEST_CASE("[FoundryCLIParser] JSON global option combines with help") {
	FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(make_args({ "foundry", "--json", "script", "format", "--help" }));
	REQUIRE_MESSAGE(result.ok, result.error);
	CHECK(result.json);
	CHECK(result.help_requested);
	CHECK_EQ(result.command_path, make_args({ "script", "format" }));
}

TEST_CASE("[FoundryCLIParser] Bare noun error records noun scope") {
	FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(make_args({ "foundry", "script" }));
	CHECK_FALSE(result.ok);
	CHECK_FALSE(result.help_requested);
	CHECK_EQ(result.command_path, make_args({ "script" }));
}

TEST_CASE("[FoundryCLIParser] Unknown verb error keeps noun scope") {
	FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(make_args({ "foundry", "script", "fmt" }));
	CHECK_FALSE(result.ok);
	CHECK_EQ(result.command_path, make_args({ "script" }));
}
```

- [ ] **Step 2: Build and verify the tests fail**

Run: `scons platform=macos target=editor dev_build=yes tests=yes && ./bin/foundry.macos.editor.dev.arm64 --headless --test --test-case="*FoundryCLIParser*"`
Expected: compile error `no member named 'help_requested'` (the struct member does not exist yet).

- [ ] **Step 3: Add `help_requested` to ParseResult**

In `main/cli_parser.h`, after `bool trusted = false;`:

```cpp
		bool trusted = false;
		bool help_requested = false;
```

- [ ] **Step 4: Add help helpers to `main/cli_parser.cpp`**

Insert after the `fail()` function (line 65):

```cpp
static bool is_help_flag(const String &p_arg) {
	return p_arg == "-h" || p_arg == "--help" || p_arg == "/?";
}

static void request_help(CLIParseState &r_state) {
	r_state.result.help_requested = true;
	r_state.result.used_new_cli = true;
}
```

- [ ] **Step 5: Detect help flags inside every verb option loop**

Insert this exact pattern as the FIRST check inside the `while` loop, immediately after `const String arg = r_state.args[r_state.index];`:

```cpp
		if (is_help_flag(arg)) {
			request_help(r_state);
			return;
		}
```

Apply at every option loop (the check must come before `consume_common_global_option` and before any passthrough `append`):
`parse_project_run` (line ~200), `parse_project_export` (~248), `parse_project_import` (~320), `parse_script_format` (~358), `parse_script_lint` (~382), `parse_script_migrate` (~414), `parse_test_run` (~521), `parse_editor` (~573), `parse_lsp` (~612), `parse_docs` — all three inner loops: `generate-api` (~649), `generate-engine` (~672), `generate-script` (~705), `parse_extension` — both inner loops: `dump-interface` (~751), `validate-api` (~781), `parse_diagnostics` (~816).

Full example for `parse_project_run` (all other sites are identical insertions):

```cpp
	while (r_state.index < r_state.args.size()) {
		const String arg = r_state.args[r_state.index];
		if (is_help_flag(arg)) {
			request_help(r_state);
			return;
		}
		if (consume_common_global_option(r_state, arg)) {
```

Because the check returns before the post-loop validation, required options (`project export --preset/--output`, `script migrate --project`, `docs generate-script --source`, `extension validate-api --input`) are correctly skipped for help requests.

- [ ] **Step 6: Detect help as the "verb" token in every noun dispatcher**

In `parse_project`, `parse_script`, `parse_test`, `parse_editor`, `parse_lsp`, `parse_docs`, `parse_extension`, `parse_diagnostics`, insert directly after `const String command = r_state.args[r_state.index++];` and BEFORE any `set_command_path(...)` call that uses `command`:

```cpp
	if (is_help_flag(command)) {
		request_help(r_state);
		return;
	}
```

This matters for `parse_editor`/`parse_lsp`/`parse_docs`/`parse_extension`/`parse_diagnostics`, which currently call `set_command_path` with the raw token — the help flag must never land in `command_path`.

- [ ] **Step 7: Record noun scope at dispatch and handle top-level help + alias**

In `FoundryCLIParser::parse`, in the dispatch block, record the noun before dispatching:

```cpp
		if (is_new_cli_command(arg)) {
			state.result.used_new_cli = true;
			state.index++;
			state.result.command_path.clear();
			append(state.result.command_path, arg);
			if (arg == "project") {
```

(`set_command_path` later overwrites this with `{noun, verb}` — unchanged behavior for successful parses; failures now retain `{noun}`.)

In the same `while` loop, ABOVE the `is_new_cli_command` check, add top-level detection:

```cpp
	while (state.index < state.args.size()) {
		const String arg = state.args[state.index];
		if (is_help_flag(arg)) {
			request_help(state);
			return state.result;
		}
		if (arg == "help") {
			request_help(state);
			for (int scope_index = state.index + 1; scope_index < state.args.size(); scope_index++) {
				append(state.result.command_path, state.args[scope_index]);
			}
			return state.result;
		}
		if (is_new_cli_command(arg)) {
```

Finally, extend the executable-detection line so `help` / `/?` as a first argument are not mistaken for an executable path:

```cpp
	state.has_executable_arg = !is_new_cli_command(first_arg) && first_arg != "help" && !is_help_flag(first_arg) && !first_arg.begins_with("-");
```

Note: a help flag that appears after a legacy-only token (e.g. `foundry --fullscreen --help`) still takes the legacy path by design — the loop returns untouched at the first unrecognized token. `Main`'s legacy `-h` handling covers that case.

- [ ] **Step 8: Build and verify all parser tests pass**

Run: `scons platform=macos target=editor dev_build=yes tests=yes && ./bin/foundry.macos.editor.dev.arm64 --headless --test --test-case="*FoundryCLIParser*"`
Expected: `Status: SUCCESS!`

- [ ] **Step 9: Commit**

```bash
git add main/cli_parser.h main/cli_parser.cpp tests/core/os/test_foundry_cli_parser.h
git commit -m "Detect help requests in Foundry CLI parser"
```

---

### Task 2: Declarative help registry and text renderers

**Goal:** `main/cli_help.{h,cpp}` — static registry describing all 8 nouns and 17 commands, plus text renderers for the three help levels, with unit tests.

**Files:**
- Create: `main/cli_help.h`
- Create: `main/cli_help.cpp` (picked up automatically — `main/SCsub` globs `*.cpp`)
- Create: `tests/core/os/test_foundry_cli_help.h`
- Modify: `tests/test_main.cpp:114` (include block)

**Acceptance Criteria:**
- [ ] Registry is plain `static const` `const char *` data — no static constructors, no STL, no exceptions
- [ ] Text renderers produce top/noun/command help with the engine's existing ANSI styling
- [ ] Editor-only commands get an `E` badge; non-editor builds omit them from listings
- [ ] Top help contains no legacy option text

**Verify:** `./bin/foundry.macos.editor.dev.arm64 --headless --test --test-case="*FoundryCLIHelp*"` → `Status: SUCCESS!`

**Steps:**

- [ ] **Step 1: Create `main/cli_help.h`**

Start with the standard Godot license header block copied verbatim from `main/cli_parser.h` (lines 1-29), with the filename comment changed to `cli_help.h`. Then:

```cpp
#pragma once

#include "core/string/ustring.h"
#include "core/variant/variant.h"

class FoundryCLIHelp {
public:
	enum Availability {
		AVAILABILITY_RELEASE,
		AVAILABILITY_EDITOR,
	};

	struct CommandOption {
		const char *flag = nullptr;
		// Null for boolean flags. "a|b" lists accepted values; the first
		// alternative doubles as a parseable sample value in drift tests.
		const char *value_name = nullptr;
		const char *description = nullptr;
		bool required = false;
		// True when the value is attached with '=' as a single token (e.g. --format=sarif).
		bool equals_form = false;
	};

	struct Positional {
		const char *name = nullptr;
		bool optional = true;
		bool repeats = false;
	};

	struct CommandSpec {
		const char *noun = nullptr;
		const char *verb = nullptr;
		const char *summary = nullptr;
		const char *usage_args = nullptr;
		Availability availability = AVAILABILITY_RELEASE;
		const CommandOption *options = nullptr;
		int option_count = 0;
		const Positional *positionals = nullptr;
		int positional_count = 0;
		const char *example = nullptr;
	};

	struct NounSpec {
		const char *name = nullptr;
		const char *summary = nullptr;
	};

	static const NounSpec *get_nouns(int &r_count);
	static const CommandSpec *get_commands(int &r_count);
	static bool has_noun(const String &p_noun);
	static bool has_command(const String &p_noun, const String &p_verb);

	static String get_top_help_text(const String &p_binary);
	static String get_noun_help_text(const String &p_noun);
	static String get_command_help_text(const String &p_noun, const String &p_verb);
	// Routes `p_scope` ([] | [noun] | [noun, verb]) to the right level.
	// `r_valid` is false when the scope names an unknown noun or verb; the
	// nearest valid level's text is still returned as a fallback.
	static String get_scoped_help_text(const String &p_binary, const PackedStringArray &p_scope, bool &r_valid);
	static String get_help_json(const PackedStringArray &p_scope);
};
```

(`get_help_json` is declared now and implemented in Task 4.)

- [ ] **Step 2: Create `main/cli_help.cpp` — registry data**

Standard license header (filename `cli_help.cpp`), then:

```cpp
#include "cli_help.h"

#include "core/io/json.h"

namespace {

using CommandOption = FoundryCLIHelp::CommandOption;
using CommandSpec = FoundryCLIHelp::CommandSpec;
using NounSpec = FoundryCLIHelp::NounSpec;
using Positional = FoundryCLIHelp::Positional;

const int HELP_OPTION_COLUMN_LENGTH = 36;

const NounSpec NOUNS[] = {
	{ "editor", "Open the editor or the Project Manager." },
	{ "project", "Run, export, or import a project." },
	{ "script", "Format, lint, and migrate Foundry Script code." },
	{ "test", "Run the engine test suites." },
	{ "lsp", "Run the Foundry Script language server." },
	{ "docs", "Generate engine and extension API documentation." },
	{ "extension", "FoundryExtension interface tooling." },
	{ "diagnostics", "Probe rendering and device support." },
};

const char *PROJECT_OPTION_DESCRIPTION = "Project directory containing a project.foundry file.";

const CommandOption EDITOR_OPEN_OPTIONS[] = {
	{ "--project", "dir", PROJECT_OPTION_DESCRIPTION, false },
};

const CommandOption PROJECT_RUN_OPTIONS[] = {
	{ "--project", "dir", PROJECT_OPTION_DESCRIPTION, false },
	{ "--scene", "path", "Path or UID of the scene to start.", false },
	{ "--script", "path", "Run the given script instead of a scene.", false },
	{ "--check-only", nullptr, "Only parse the script for errors and quit.", false },
};

const CommandOption PROJECT_EXPORT_OPTIONS[] = {
	{ "--project", "dir", PROJECT_OPTION_DESCRIPTION, false },
	{ "--preset", "name", "Export preset name from export_presets.cfg.", true },
	{ "--output", "path", "Output path, including the binary filename.", true },
	{ "--mode", "release|debug|pack|patch", "Export mode (default: release).", false },
	{ "--patches", "paths", "Comma-separated list of patch packs (used with --mode patch).", false },
	{ "--install-android-build-template", nullptr, "Install the Android build template before exporting.", false },
};

const CommandOption PROJECT_IMPORT_OPTIONS[] = {
	{ "--project", "dir", PROJECT_OPTION_DESCRIPTION, false },
};

const CommandOption SCRIPT_FORMAT_OPTIONS[] = {
	{ "--project", "dir", PROJECT_OPTION_DESCRIPTION, false },
	{ "--check", nullptr, "Exit non-zero if any file would change; do not write.", false },
	{ "--write", nullptr, "Rewrite formatted files in place.", false },
	{ "--diff", nullptr, "Print a unified diff of pending changes.", false },
};

const CommandOption SCRIPT_LINT_OPTIONS[] = {
	{ "--project", "dir", PROJECT_OPTION_DESCRIPTION, false },
	{ "--format", "json|sarif", "Machine-readable report format.", false, true },
	{ "--out", "path", "Write the report to a file instead of stdout.", false },
	{ "--fail-on", "error|warning", "Severity threshold for a non-zero exit code.", false, true },
};

const CommandOption SCRIPT_MIGRATE_OPTIONS[] = {
	{ "--project", "dir", PROJECT_OPTION_DESCRIPTION, true },
	{ "--apply", nullptr, "Commit inferred type annotations to disk (otherwise dry-run).", false },
	{ "--strict", "null,dynamic", "Strict checks to apply to the project, comma-separated.", false },
	{ "--activate-strict", nullptr, "Flip the requested strict project settings.", false },
	{ "--confirm", nullptr, "Confirm the gated strict-settings flip.", false },
	{ "--allow-violations", nullptr, "Allow the strict flip even when violations remain.", false },
	{ "--acknowledge-vcs", nullptr, "Proceed with --apply on an unversioned or dirty tree.", false },
	{ "--follow-up", "path", "Write the manual follow-up punch list to a file.", false },
};

const CommandOption TEST_RUN_OPTIONS[] = {
	{ "--project", "dir", PROJECT_OPTION_DESCRIPTION, false },
	{ "--case", "pattern", "doctest test case filter pattern.", false },
};

const CommandOption LSP_SERVE_OPTIONS[] = {
	{ "--project", "dir", PROJECT_OPTION_DESCRIPTION, false },
	{ "--port", "port", "LSP port. Recommended range [1024, 49151].", false },
};

const CommandOption DOCS_GENERATE_API_OPTIONS[] = {
	{ "--include-docs", nullptr, "Include class documentation in the dump.", false },
};

const CommandOption DOCS_GENERATE_ENGINE_OPTIONS[] = {
	{ "--output", "path", "Output directory (default: current directory).", false },
	{ "--no-docbase", nullptr, "Do not dump the base types.", false },
};

const CommandOption DOCS_GENERATE_SCRIPT_OPTIONS[] = {
	{ "--source", "path", "Directory containing Foundry Script files to document.", true },
	{ "--output", "path", "Output directory (default: current directory).", false },
};

const CommandOption EXTENSION_DUMP_INTERFACE_OPTIONS[] = {
	{ "--format", "header|json", "Interface dump format (default: header).", false },
};

const CommandOption EXTENSION_VALIDATE_API_OPTIONS[] = {
	{ "--input", "path", "Extension API JSON file from a previous engine version.", true },
};

const Positional PATHS_POSITIONAL[] = {
	{ "paths", true, true },
};

const Positional DOCTEST_ARGS_POSITIONAL[] = {
	{ "doctest-args", true, true },
};

#define FOUNDRY_CLI_COUNT(m_array) ((int)(sizeof(m_array) / sizeof((m_array)[0])))

const CommandSpec COMMANDS[] = {
	{ "editor", "open", "Open a project in the editor.", "[--project <dir>]", FoundryCLIHelp::AVAILABILITY_EDITOR, EDITOR_OPEN_OPTIONS, FOUNDRY_CLI_COUNT(EDITOR_OPEN_OPTIONS), nullptr, 0, "foundry editor open --project ." },
	{ "editor", "project-manager", "Open the Project Manager.", "", FoundryCLIHelp::AVAILABILITY_EDITOR, nullptr, 0, nullptr, 0, "foundry editor project-manager" },
	{ "project", "run", "Run a project; arguments after -- go to the project.", "[--project <dir>] [--scene <path>] [--script <path>] [--check-only] [-- <user args...>]", FoundryCLIHelp::AVAILABILITY_RELEASE, PROJECT_RUN_OPTIONS, FOUNDRY_CLI_COUNT(PROJECT_RUN_OPTIONS), nullptr, 0, "foundry project run --project . --scene res://main.tscn -- --difficulty hard" },
	{ "project", "export", "Export a project with a preset.", "[--project <dir>] --preset <name> --output <path> [--mode <release|debug|pack|patch>] [--patches <paths>] [--install-android-build-template]", FoundryCLIHelp::AVAILABILITY_EDITOR, PROJECT_EXPORT_OPTIONS, FOUNDRY_CLI_COUNT(PROJECT_EXPORT_OPTIONS), nullptr, 0, "foundry project export --project . --preset Linux --output build/game.x86_64 --mode release" },
	{ "project", "import", "Import project resources and exit.", "[--project <dir>]", FoundryCLIHelp::AVAILABILITY_EDITOR, PROJECT_IMPORT_OPTIONS, FOUNDRY_CLI_COUNT(PROJECT_IMPORT_OPTIONS), nullptr, 0, "foundry project import --project ." },
	{ "script", "format", "Format Foundry Script files or stdin.", "[--project <dir>] [--check|--write|--diff] [paths...]", FoundryCLIHelp::AVAILABILITY_RELEASE, SCRIPT_FORMAT_OPTIONS, FOUNDRY_CLI_COUNT(SCRIPT_FORMAT_OPTIONS), PATHS_POSITIONAL, FOUNDRY_CLI_COUNT(PATHS_POSITIONAL), "foundry script format --project . --check scripts" },
	{ "script", "lint", "Lint Foundry Script files.", "[--project <dir>] [--format=<json|sarif>] [--out <path>] [--fail-on=<error|warning>] [paths...]", FoundryCLIHelp::AVAILABILITY_RELEASE, SCRIPT_LINT_OPTIONS, FOUNDRY_CLI_COUNT(SCRIPT_LINT_OPTIONS), PATHS_POSITIONAL, FOUNDRY_CLI_COUNT(PATHS_POSITIONAL), "foundry script lint --project . --format=sarif --out reports/foundry-script.sarif scripts" },
	{ "script", "migrate", "Run the Foundry Script strict-typing migration wizard.", "--project <dir> [--apply] [--strict <null,dynamic>] [--activate-strict] [--confirm] [--allow-violations] [--acknowledge-vcs] [--follow-up <path>]", FoundryCLIHelp::AVAILABILITY_EDITOR, SCRIPT_MIGRATE_OPTIONS, FOUNDRY_CLI_COUNT(SCRIPT_MIGRATE_OPTIONS), nullptr, 0, "foundry script migrate --trusted --project . --apply --strict null,dynamic --confirm" },
	{ "test", "run", "Run the engine doctest suites.", "[--project <dir>] [--case <pattern>] [doctest-args...]", FoundryCLIHelp::AVAILABILITY_RELEASE, TEST_RUN_OPTIONS, FOUNDRY_CLI_COUNT(TEST_RUN_OPTIONS), DOCTEST_ARGS_POSITIONAL, FOUNDRY_CLI_COUNT(DOCTEST_ARGS_POSITIONAL), "foundry test run --case \"*FoundryScript*\"" },
	{ "lsp", "serve", "Start the Foundry Script language server.", "[--project <dir>] [--port <port>]", FoundryCLIHelp::AVAILABILITY_EDITOR, LSP_SERVE_OPTIONS, FOUNDRY_CLI_COUNT(LSP_SERVE_OPTIONS), nullptr, 0, "foundry lsp serve --project . --port 6005" },
	{ "docs", "generate-api", "Generate the extension API JSON dump.", "[--include-docs]", FoundryCLIHelp::AVAILABILITY_EDITOR, DOCS_GENERATE_API_OPTIONS, FOUNDRY_CLI_COUNT(DOCS_GENERATE_API_OPTIONS), nullptr, 0, "foundry docs generate-api --include-docs" },
	{ "docs", "generate-engine", "Dump the engine class reference XML.", "[--output <path>] [--no-docbase]", FoundryCLIHelp::AVAILABILITY_EDITOR, DOCS_GENERATE_ENGINE_OPTIONS, FOUNDRY_CLI_COUNT(DOCS_GENERATE_ENGINE_OPTIONS), nullptr, 0, "foundry docs generate-engine --output doc-out" },
	{ "docs", "generate-script", "Generate API reference from Foundry Script sources.", "--source <path> [--output <path>]", FoundryCLIHelp::AVAILABILITY_EDITOR, DOCS_GENERATE_SCRIPT_OPTIONS, FOUNDRY_CLI_COUNT(DOCS_GENERATE_SCRIPT_OPTIONS), nullptr, 0, "foundry docs generate-script --source addons/library" },
	{ "extension", "dump-interface", "Generate FoundryExtension interface files.", "[--format <header|json>]", FoundryCLIHelp::AVAILABILITY_EDITOR, EXTENSION_DUMP_INTERFACE_OPTIONS, FOUNDRY_CLI_COUNT(EXTENSION_DUMP_INTERFACE_OPTIONS), nullptr, 0, "foundry extension dump-interface --format json" },
	{ "extension", "validate-api", "Validate an extension API dump for compatibility.", "--input <path>", FoundryCLIHelp::AVAILABILITY_EDITOR, EXTENSION_VALIDATE_API_OPTIONS, FOUNDRY_CLI_COUNT(EXTENSION_VALIDATE_API_OPTIONS), nullptr, 0, "foundry extension validate-api --input extension_api.json" },
	{ "diagnostics", "render-device-support", "Probe rendering device support.", "", FoundryCLIHelp::AVAILABILITY_RELEASE, nullptr, 0, nullptr, 0, "foundry diagnostics render-device-support" },
	{ "diagnostics", "render-device-create", "Attempt rendering device creation.", "", FoundryCLIHelp::AVAILABILITY_RELEASE, nullptr, 0, nullptr, 0, "foundry diagnostics render-device-create" },
};

const CommandOption GLOBAL_OPTIONS[] = {
	{ "--project", "dir", PROJECT_OPTION_DESCRIPTION, false },
	{ "--json", nullptr, "Machine-readable output where supported; suppresses the startup header.", false },
	{ "--trusted", nullptr, "Allow project-defined build task execution.", false },
	{ "--headless", nullptr, "Headless mode (no display, dummy audio).", false },
	{ "--quiet", nullptr, "Silence stdout messages; errors are still shown.", false },
	{ "--verbose", nullptr, "Verbose stdout mode.", false },
	{ "--no-header", nullptr, "Do not print the engine version header on startup.", false },
	{ "--version", nullptr, "Print the version string and exit.", false },
	{ "-h, --help", nullptr, "Print help; combine with a command for scoped help.", false },
};
```

- [ ] **Step 3: Add rendering helpers and renderers to `main/cli_help.cpp`**

Continue inside the anonymous namespace:

```cpp
bool is_command_in_build(const CommandSpec &p_spec) {
#ifdef TOOLS_ENABLED
	return true;
#else
	return p_spec.availability == FoundryCLIHelp::AVAILABILITY_RELEASE;
#endif
}

bool is_noun_in_build(const String &p_noun) {
	for (const CommandSpec &spec : COMMANDS) {
		if (p_noun == spec.noun && is_command_in_build(spec)) {
			return true;
		}
	}
	return false;
}

String help_title(const String &p_title) {
	return "\n\u001b[1;93m" + p_title + ":\u001b[0m\n";
}

// Two-column row matching the engine's historical help styling: green name
// column with magenta/cyan placeholder coloring, optional red E badge.
String help_item(const String &p_name, const String &p_description, bool p_editor_badge) {
	const String badge = p_editor_badge ? String("\u001b[1;91mE\u001b[0m") : String(" ");
	const String column = p_name.rpad(HELP_OPTION_COLUMN_LENGTH)
			.replace("[", "\u001b[96m[")
			.replace("]", "]\u001b[0m")
			.replace("<", "\u001b[95m<")
			.replace(">", ">\u001b[0m");
	return "  \u001b[92m" + column + "\u001b[0m " + badge + "  " + p_description + "\n";
}

String editor_badge_legend() {
#ifdef TOOLS_ENABLED
	return "\n  \u001b[1;91mE\u001b[0m  Only available in editor builds.\n";
#else
	return String();
#endif
}

String option_display(const CommandOption &p_option) {
	String display = p_option.flag;
	if (p_option.value_name) {
		display += (p_option.equals_form ? "=<" : " <") + String(p_option.value_name) + ">";
	}
	return display;
}

const CommandSpec *find_command(const String &p_noun, const String &p_verb) {
	for (const CommandSpec &spec : COMMANDS) {
		if (p_noun == spec.noun && p_verb == spec.verb) {
			return &spec;
		}
	}
	return nullptr;
}

String usage_line(const CommandSpec &p_spec) {
	String usage = "foundry " + String(p_spec.noun) + " " + String(p_spec.verb);
	if (p_spec.usage_args && p_spec.usage_args[0]) {
		usage += " " + String(p_spec.usage_args);
	}
	return usage;
}

} // namespace

const FoundryCLIHelp::NounSpec *FoundryCLIHelp::get_nouns(int &r_count) {
	r_count = FOUNDRY_CLI_COUNT(NOUNS);
	return NOUNS;
}

const FoundryCLIHelp::CommandSpec *FoundryCLIHelp::get_commands(int &r_count) {
	r_count = FOUNDRY_CLI_COUNT(COMMANDS);
	return COMMANDS;
}

bool FoundryCLIHelp::has_noun(const String &p_noun) {
	for (const NounSpec &noun : NOUNS) {
		if (p_noun == noun.name) {
			return true;
		}
	}
	return false;
}

bool FoundryCLIHelp::has_command(const String &p_noun, const String &p_verb) {
	return find_command(p_noun, p_verb) != nullptr;
}

String FoundryCLIHelp::get_top_help_text(const String &p_binary) {
	String text;
	text += help_title("Usage");
	text += "  " + p_binary + " \u001b[96m<command> <subcommand> [options] [-- user args]\u001b[0m\n";
	text += help_title("Commands");
	for (const NounSpec &noun : NOUNS) {
		if (!is_noun_in_build(noun.name)) {
			continue;
		}
		text += help_item(noun.name, noun.summary, false);
	}
	text += help_title("Global options");
	for (const CommandOption &option : GLOBAL_OPTIONS) {
		text += help_item(option_display(option), option.description, false);
	}
	text += "\nRun 'foundry <command> --help' for command details.\n";
	return text;
}

String FoundryCLIHelp::get_noun_help_text(const String &p_noun) {
	String text;
	text += help_title("Usage");
	text += "  foundry " + p_noun + " \u001b[96m<subcommand> [options]\u001b[0m\n";
	text += help_title("Subcommands");
	bool any_editor_badge = false;
	for (const CommandSpec &spec : COMMANDS) {
		if (p_noun != spec.noun || !is_command_in_build(spec)) {
			continue;
		}
		const bool editor_badge = spec.availability == AVAILABILITY_EDITOR;
		any_editor_badge = any_editor_badge || editor_badge;
		text += help_item(spec.verb, spec.summary, editor_badge);
	}
	if (any_editor_badge) {
		text += editor_badge_legend();
	}
	text += "\nRun 'foundry " + p_noun + " <subcommand> --help' for details.\n";
	return text;
}

String FoundryCLIHelp::get_command_help_text(const String &p_noun, const String &p_verb) {
	const CommandSpec *spec = find_command(p_noun, p_verb);
	if (spec == nullptr) {
		return String();
	}
	String text;
	text += "\n\u001b[92mfoundry " + p_noun + " " + p_verb + "\u001b[0m - " + spec->summary;
	if (spec->availability == AVAILABILITY_EDITOR) {
		text += " \u001b[1;91m[editor builds only]\u001b[0m";
	}
	text += "\n";
	text += help_title("Usage");
	text += "  " + usage_line(*spec) + "\n";
	if (spec->option_count > 0) {
		text += help_title("Options");
		for (int i = 0; i < spec->option_count; i++) {
			const CommandOption &option = spec->options[i];
			String description = option.description;
			if (option.required) {
				description += " (required)";
			}
			text += help_item(option_display(option), description, false);
		}
	}
	if (spec->example) {
		text += help_title("Example");
		text += "  " + String(spec->example) + "\n";
	}
	return text;
}

String FoundryCLIHelp::get_scoped_help_text(const String &p_binary, const PackedStringArray &p_scope, bool &r_valid) {
	r_valid = true;
	if (p_scope.is_empty()) {
		return get_top_help_text(p_binary);
	}
	if (!has_noun(p_scope[0])) {
		r_valid = false;
		return get_top_help_text(p_binary);
	}
	if (p_scope.size() == 1) {
		return get_noun_help_text(p_scope[0]);
	}
	if (p_scope.size() == 2 && has_command(p_scope[0], p_scope[1])) {
		return get_command_help_text(p_scope[0], p_scope[1]);
	}
	r_valid = false;
	return get_noun_help_text(p_scope[0]);
}
```

Also add a temporary stub so the header link succeeds until Task 4:

```cpp
String FoundryCLIHelp::get_help_json(const PackedStringArray &p_scope) {
	return String("{}");
}
```

- [ ] **Step 4: Create `tests/core/os/test_foundry_cli_help.h`**

Standard license header (filename `test_foundry_cli_help.h`), then:

```cpp
#pragma once

#include "main/cli_help.h"
#include "main/cli_parser.h"

#include "tests/test_macros.h"

namespace TestFoundryCLIHelp {

TEST_CASE("[FoundryCLIHelp] Top help lists nouns and omits legacy options") {
	const String text = FoundryCLIHelp::get_top_help_text("foundry");
	int noun_count = 0;
	const FoundryCLIHelp::NounSpec *nouns = FoundryCLIHelp::get_nouns(noun_count);
	for (int i = 0; i < noun_count; i++) {
		CHECK_MESSAGE(text.contains(nouns[i].name), nouns[i].name);
	}
	CHECK(text.contains("--json"));
	CHECK(text.contains("Run 'foundry <command> --help'"));
	CHECK_FALSE(text.contains("--resolution"));
	CHECK_FALSE(text.contains("--fullscreen"));
	CHECK_FALSE(text.contains("--doctool"));
	CHECK_FALSE(text.contains("--export-release"));
}

TEST_CASE("[FoundryCLIHelp] Noun help lists its subcommands") {
	const String text = FoundryCLIHelp::get_noun_help_text("script");
	CHECK(text.contains("format"));
	CHECK(text.contains("lint"));
#ifdef TOOLS_ENABLED
	CHECK(text.contains("migrate"));
#endif
	CHECK(text.contains("Run 'foundry script <subcommand> --help'"));
}

TEST_CASE("[FoundryCLIHelp] Command help documents options and example") {
	const String text = FoundryCLIHelp::get_command_help_text("script", "format");
	CHECK(text.contains("--check"));
	CHECK(text.contains("--write"));
	CHECK(text.contains("--diff"));
	CHECK(text.contains("foundry script format --project . --check scripts"));
}

TEST_CASE("[FoundryCLIHelp] Scoped routing validates nouns and verbs") {
	bool valid = false;

	FoundryCLIHelp::get_scoped_help_text("foundry", PackedStringArray(), valid);
	CHECK(valid);

	PackedStringArray noun_scope;
	noun_scope.push_back("script");
	FoundryCLIHelp::get_scoped_help_text("foundry", noun_scope, valid);
	CHECK(valid);

	PackedStringArray verb_scope = noun_scope;
	verb_scope.push_back("format");
	FoundryCLIHelp::get_scoped_help_text("foundry", verb_scope, valid);
	CHECK(valid);

	PackedStringArray bad_noun;
	bad_noun.push_back("scritp");
	const String fallback_top = FoundryCLIHelp::get_scoped_help_text("foundry", bad_noun, valid);
	CHECK_FALSE(valid);
	CHECK(fallback_top.contains("Global options"));

	PackedStringArray bad_verb = noun_scope;
	bad_verb.push_back("fmt");
	const String fallback_noun = FoundryCLIHelp::get_scoped_help_text("foundry", bad_verb, valid);
	CHECK_FALSE(valid);
	CHECK(fallback_noun.contains("Subcommands"));
}

} // namespace TestFoundryCLIHelp
```

- [ ] **Step 5: Wire the test header into `tests/test_main.cpp`**

At line 114, keep alphabetical order:

```cpp
#include "tests/core/os/test_foundry_cli_help.h"
#include "tests/core/os/test_foundry_cli_parser.h"
```

- [ ] **Step 6: Build and verify**

Run: `scons platform=macos target=editor dev_build=yes tests=yes && ./bin/foundry.macos.editor.dev.arm64 --headless --test --test-case="*FoundryCLIHelp*"`
Expected: `Status: SUCCESS!`

- [ ] **Step 7: Commit**

```bash
git add main/cli_help.h main/cli_help.cpp tests/core/os/test_foundry_cli_help.h tests/test_main.cpp
git commit -m "Add declarative CLI help registry and renderers"
```

---

### Task 3: Wire scoped help into Main and delete legacy help sections

**Goal:** `foundry --help` shows only nouns + global options; `script --help` / `script format --help` show scoped help; errors print scoped help to stderr; all legacy option sections are deleted.

**Files:**
- Modify: `main/main.h:40-53` (remove dead help declarations)
- Modify: `main/main.cpp` (includes at :61, `OPTION_COLUMN_LENGTH` at :309, help helpers at :474-541, `print_help` body at :542-817, `test_entrypoint` at :1036, `setup()` at :1287)

**Acceptance Criteria:**
- [ ] `foundry --help` output contains no legacy sections (no "Display options", "Debug options", "Standalone tools", "Run options")
- [ ] `--help` exits 0 (stdout); bare noun / unknown verb exit non-zero with scoped help on stderr
- [ ] `Main::print_help` delegates to the registry renderer; dead helpers removed
- [ ] Full test suite still passes

**Verify:** `./bin/foundry.macos.editor.dev.arm64 --help` shows the short noun listing; `./bin/foundry.macos.editor.dev.arm64 --headless --test --force-colors` → `Status: SUCCESS!`

**Steps:**

- [ ] **Step 1: Include the registry in `main/main.cpp`**

After `#include "main/cli_parser.h"` (line 61):

```cpp
#include "main/cli_help.h"
```

- [ ] **Step 2: Replace the `print_help` body**

Replace everything from `void Main::print_help(const char *p_binary) {` (line 542) through the end of that function (line 817) with:

```cpp
void Main::print_help(const char *p_binary) {
	print_header(true);
	print_help_copyright("Free and open source software under the terms of the MIT license.");
	print_help_copyright("(c) 2014-present Godot Engine contributors. (c) 2007-present Juan Linietsky, Ariel Manzur.");
	OS::get_singleton()->print("%s", FoundryCLIHelp::get_top_help_text(p_binary).utf8().get_data());
}
```

- [ ] **Step 3: Delete the now-dead help machinery**

- Delete `Main::print_help_title` (main.cpp:482-484), `Main::format_help_option` (:490-497), and `Main::print_help_option` (:506-540). Keep `print_help_copyright` (still used) and `print_header`.
- Delete `static const int OPTION_COLUMN_LENGTH = 32;` (main.cpp:309).
- In `main/main.h`, delete the `CLIOptionAvailability` enum (lines 40-48) and the declarations of `print_help_title`, `print_help_option`, `format_help_option` (lines 50-52). Keep `print_help_copyright` and `print_help`.
- Run `grep -rn "print_help_option\|print_help_title\|format_help_option\|CLI_OPTION_AVAILABILITY" --include="*.cpp" --include="*.h" .` — expect zero hits outside deleted code; fix any stragglers found.

- [ ] **Step 4: Dispatch help in `Main::setup()`**

Replace the parse-result handling at main.cpp:1287-1291 with:

```cpp
	const FoundryCLIParser::ParseResult cli_parse = FoundryCLIParser::parse(raw_cli_args);
	if (!cli_parse.ok) {
		OS::get_singleton()->printerr("Foundry CLI error: %s\n", cli_parse.error.utf8().get_data());
		if (!cli_parse.command_path.is_empty() && FoundryCLIHelp::has_noun(cli_parse.command_path[0])) {
			bool scope_valid = false;
			const String scoped_help = FoundryCLIHelp::get_scoped_help_text(execpath, cli_parse.command_path, scope_valid);
			OS::get_singleton()->printerr("%s", scoped_help.utf8().get_data());
		}
		goto error;
	}
	if (cli_parse.help_requested) {
		bool scope_valid = false;
		const String scoped_help = FoundryCLIHelp::get_scoped_help_text(execpath, cli_parse.command_path, scope_valid);
		if (!scope_valid) {
			OS::get_singleton()->printerr("Unknown command for help: %s\n", String(" ").join(cli_parse.command_path).utf8().get_data());
			OS::get_singleton()->printerr("%s", scoped_help.utf8().get_data());
			goto error;
		}
		if (cli_parse.command_path.is_empty()) {
			print_help(execpath);
		} else {
			print_header(true);
			OS::get_singleton()->print("%s", scoped_help.utf8().get_data());
		}
		exit_err = ERR_HELP;
		goto error;
	}
```

Notes: `ERR_HELP` is already special-cased by the platform entry points to exit 0; the `!ok` path keeps `exit_err = ERR_INVALID_PARAMETER` (non-zero exit). New locals are declared inside the `if` blocks so the later `goto error` statements do not jump over initializations. The legacy `-h/--help//?` branch at main.cpp:1391 stays — it now renders the new top help via the rewritten `print_help`.

- [ ] **Step 5: Short-circuit help in `test_entrypoint`**

In `Main::test_entrypoint` (main.cpp:1036), after the existing `if (!cli_parse.ok)` block:

```cpp
	if (cli_parse.help_requested) {
		tests_need_run = false;
		return EXIT_SUCCESS;
	}
```

(Startup then proceeds to `Main::setup()`, which prints the help. Without this, `foundry test run --help` would depend on raw-args scanning not finding `--test`.)

- [ ] **Step 6: Build, run the full suite, and smoke-test by hand**

Run: `scons platform=macos target=editor dev_build=yes tests=yes && ./bin/foundry.macos.editor.dev.arm64 --headless --test --force-colors`
Expected: `Status: SUCCESS!`

Then smoke-test:

```bash
BIN=./bin/foundry.macos.editor.dev.arm64
$BIN --help                       # nouns + global options only, exit 0
$BIN script --help                # format/lint/migrate listing, exit 0
$BIN script format --help         # options + example, exit 0
$BIN help script format           # same as previous
$BIN script; echo "exit=$?"       # noun help on stderr, exit != 0
$BIN script fmt; echo "exit=$?"   # "Unknown script command" + noun help on stderr, exit != 0
$BIN --help | grep -cE "resolution|fullscreen|Debug options"   # expect 0
```

- [ ] **Step 7: Commit**

```bash
git add main/main.h main/main.cpp
git commit -m "Route --help through scoped CLI help and drop legacy help text"
```

---

### Task 4: JSON help output

**Goal:** `--json` + help emits schema v1 JSON (full tree or scoped), no header, only compiled-in commands.

**Files:**
- Modify: `main/cli_help.cpp` (replace the `get_help_json` stub)
- Modify: `main/main.cpp` (JSON branch in the setup help dispatch from Task 3)
- Test: `tests/core/os/test_foundry_cli_help.h`

**Acceptance Criteria:**
- [ ] `foundry --json --help` emits parseable JSON with `foundry_cli_help_version: 1` and one flat `commands` array
- [ ] `foundry --json script format --help` emits exactly one command entry
- [ ] Output contains no ANSI escapes and no startup header

**Verify:** `./bin/foundry.macos.editor.dev.arm64 --headless --test --test-case="*FoundryCLIHelp*"` → `Status: SUCCESS!`; `./bin/foundry.macos.editor.dev.arm64 --json script format --help | python3 -m json.tool`

**Steps:**

- [ ] **Step 1: Write the failing tests**

Append to `tests/core/os/test_foundry_cli_help.h` (inside the namespace):

```cpp
TEST_CASE("[FoundryCLIHelp] JSON help is valid and versioned") {
	JSON json;
	REQUIRE_EQ(json.parse(FoundryCLIHelp::get_help_json(PackedStringArray())), OK);
	const Dictionary root = json.get_data();
	CHECK_EQ(int(root["foundry_cli_help_version"]), 1);
	const Array commands = root["commands"];
#ifdef TOOLS_ENABLED
	int command_count = 0;
	FoundryCLIHelp::get_commands(command_count);
	CHECK_EQ(commands.size(), command_count);
#else
	CHECK(commands.size() > 0);
#endif
	const Dictionary first = commands[0];
	for (const char *key : { "path", "summary", "usage", "availability", "options", "positionals", "examples" }) {
		CHECK_MESSAGE(first.has(key), key);
	}
}

TEST_CASE("[FoundryCLIHelp] JSON help scopes to a single command") {
	PackedStringArray scope;
	scope.push_back("script");
	scope.push_back("format");
	JSON json;
	REQUIRE_EQ(json.parse(FoundryCLIHelp::get_help_json(scope)), OK);
	const Dictionary root = json.get_data();
	const Array commands = root["commands"];
	REQUIRE_EQ(commands.size(), 1);
	const Dictionary entry = commands[0];
	const Array path = entry["path"];
	REQUIRE_EQ(path.size(), 2);
	CHECK_EQ(String(path[0]), "script");
	CHECK_EQ(String(path[1]), "format");
	const Array options = entry["options"];
	CHECK(options.size() > 0);
}
```

Add `#include "core/io/json.h"` to the test header's include block.

- [ ] **Step 2: Build and verify the new tests fail**

Run: `scons platform=macos target=editor dev_build=yes tests=yes && ./bin/foundry.macos.editor.dev.arm64 --headless --test --test-case="*FoundryCLIHelp*"`
Expected: FAIL — the stub returns `{}`, so `foundry_cli_help_version` is missing.

- [ ] **Step 3: Implement `get_help_json`**

Replace the Task 2 stub in `main/cli_help.cpp` with:

```cpp
String FoundryCLIHelp::get_help_json(const PackedStringArray &p_scope) {
	Array commands_json;
	for (const CommandSpec &spec : COMMANDS) {
		if (!is_command_in_build(spec)) {
			continue;
		}
		if (p_scope.size() >= 1 && p_scope[0] != spec.noun) {
			continue;
		}
		if (p_scope.size() >= 2 && p_scope[1] != spec.verb) {
			continue;
		}
		Dictionary entry;
		Array path;
		path.push_back(spec.noun);
		path.push_back(spec.verb);
		entry["path"] = path;
		entry["summary"] = spec.summary;
		entry["usage"] = usage_line(spec);
		entry["availability"] = spec.availability == AVAILABILITY_EDITOR ? "editor" : "release";
		Array options;
		for (int i = 0; i < spec.option_count; i++) {
			const CommandOption &option = spec.options[i];
			Dictionary option_json;
			option_json["flag"] = option.flag;
			option_json["value"] = option.value_name ? Variant(String(option.value_name)) : Variant();
			option_json["style"] = option.value_name ? Variant(String(option.equals_form ? "equals" : "space")) : Variant();
			option_json["description"] = option.description;
			option_json["required"] = option.required;
			options.push_back(option_json);
		}
		entry["options"] = options;
		Array positionals;
		for (int i = 0; i < spec.positional_count; i++) {
			const Positional &positional = spec.positionals[i];
			Dictionary positional_json;
			positional_json["name"] = positional.name;
			positional_json["optional"] = positional.optional;
			positional_json["repeats"] = positional.repeats;
			positionals.push_back(positional_json);
		}
		entry["positionals"] = positionals;
		Array examples;
		if (spec.example) {
			examples.push_back(spec.example);
		}
		entry["examples"] = examples;
		commands_json.push_back(entry);
	}
	Dictionary root;
	root["foundry_cli_help_version"] = 1;
	root["commands"] = commands_json;
	return JSON::stringify(root, "  ", false);
}
```

An unknown scope yields an empty `commands` array rather than an error — agents can detect emptiness; document in Task 6.

- [ ] **Step 4: Add the JSON branch to the setup help dispatch**

In the `if (cli_parse.help_requested)` block added in Task 3, insert as the first statement:

```cpp
	if (cli_parse.help_requested) {
		if (cli_parse.json) {
			OS::get_singleton()->print("%s\n", FoundryCLIHelp::get_help_json(cli_parse.command_path).utf8().get_data());
			exit_err = ERR_HELP;
			goto error;
		}
		bool scope_valid = false;
```

(No `print_header` call on this path — the header would corrupt the JSON document.)

- [ ] **Step 5: Build, verify tests pass, and smoke-test**

Run: `scons platform=macos target=editor dev_build=yes tests=yes && ./bin/foundry.macos.editor.dev.arm64 --headless --test --test-case="*FoundryCLIHelp*"`
Expected: `Status: SUCCESS!`

```bash
./bin/foundry.macos.editor.dev.arm64 --json --help | python3 -m json.tool > /dev/null && echo VALID
./bin/foundry.macos.editor.dev.arm64 --json script format --help | python3 -m json.tool
```
Expected: `VALID`, then a single-command JSON document.

- [ ] **Step 6: Commit**

```bash
git add main/cli_help.cpp main/main.cpp tests/core/os/test_foundry_cli_help.h
git commit -m "Emit machine-readable JSON help for Foundry CLI"
```

---

### Task 5: Anti-drift cross-check tests

**Goal:** Doctest proof that every registry entry is honored by the parser — the docopt zero-drift property.

**Files:**
- Test: `tests/core/os/test_foundry_cli_help.h`

**Acceptance Criteria:**
- [ ] Every `NounSpec` is accepted by `FoundryCLIParser::is_new_cli_command`; noun count is pinned at 8
- [ ] Every `CommandSpec` parses (with required options supplied) and dispatches to the matching `command_path`
- [ ] Every documented option is accepted by its command's parse function

**Verify:** `./bin/foundry.macos.editor.dev.arm64 --headless --test --test-case="*FoundryCLIHelp*"` → `Status: SUCCESS!`

**Steps:**

- [ ] **Step 1: Add drift helpers and tests**

Append inside `namespace TestFoundryCLIHelp`:

```cpp
// The first "|" alternative of value_name doubles as a sample value the
// parser must accept (e.g. "release|debug|pack|patch" -> "release").
static String drift_option_value(const FoundryCLIHelp::CommandOption &p_option) {
	return String(p_option.value_name).get_slice("|", 0);
}

static void drift_append_option(PackedStringArray &r_args, const FoundryCLIHelp::CommandOption &p_option) {
	if (p_option.value_name && p_option.equals_form) {
		r_args.push_back(String(p_option.flag) + "=" + drift_option_value(p_option));
		return;
	}
	r_args.push_back(p_option.flag);
	if (p_option.value_name) {
		r_args.push_back(drift_option_value(p_option));
	}
}

static PackedStringArray drift_base_args(const FoundryCLIHelp::CommandSpec &p_spec) {
	PackedStringArray args;
	args.push_back("foundry");
	args.push_back(p_spec.noun);
	args.push_back(p_spec.verb);
	for (int i = 0; i < p_spec.option_count; i++) {
		const FoundryCLIHelp::CommandOption &option = p_spec.options[i];
		if (!option.required) {
			continue;
		}
		drift_append_option(args, option);
	}
	return args;
}

TEST_CASE("[FoundryCLIHelp] Registry nouns match the parser") {
	int noun_count = 0;
	const FoundryCLIHelp::NounSpec *nouns = FoundryCLIHelp::get_nouns(noun_count);
	CHECK_EQ(noun_count, 8);
	for (int i = 0; i < noun_count; i++) {
		CHECK_MESSAGE(FoundryCLIParser::is_new_cli_command(nouns[i].name), nouns[i].name);
	}
	int command_count = 0;
	const FoundryCLIHelp::CommandSpec *commands = FoundryCLIHelp::get_commands(command_count);
	for (int i = 0; i < command_count; i++) {
		CHECK_MESSAGE(FoundryCLIHelp::has_noun(commands[i].noun), commands[i].noun);
	}
}

TEST_CASE("[FoundryCLIHelp] Every registry command is accepted by the parser") {
	int command_count = 0;
	const FoundryCLIHelp::CommandSpec *commands = FoundryCLIHelp::get_commands(command_count);
	for (int i = 0; i < command_count; i++) {
		const FoundryCLIHelp::CommandSpec &spec = commands[i];
		const String label = String(spec.noun) + " " + spec.verb;
		FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(drift_base_args(spec));
		REQUIRE_MESSAGE(result.ok, label + ": " + result.error);
		PackedStringArray expected_path;
		expected_path.push_back(spec.noun);
		expected_path.push_back(spec.verb);
		CHECK_MESSAGE(result.command_path == expected_path, label);
	}
}

TEST_CASE("[FoundryCLIHelp] Every documented option is accepted by its parser") {
	int command_count = 0;
	const FoundryCLIHelp::CommandSpec *commands = FoundryCLIHelp::get_commands(command_count);
	for (int i = 0; i < command_count; i++) {
		const FoundryCLIHelp::CommandSpec &spec = commands[i];
		for (int option_index = 0; option_index < spec.option_count; option_index++) {
			const FoundryCLIHelp::CommandOption &option = spec.options[option_index];
			PackedStringArray args = drift_base_args(spec);
			if (!option.required) {
				drift_append_option(args, option);
			}
			FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(args);
			REQUIRE_MESSAGE(result.ok, String(spec.noun) + " " + spec.verb + " " + option.flag + ": " + result.error);
		}
	}
}
```

Known limitation (documented in the spec): the reverse direction — an option the parser accepts but the registry omits — is not mechanically detectable until parsing becomes table-driven (issue #830).

- [ ] **Step 2: Build and verify**

Run: `scons platform=macos target=editor dev_build=yes tests=yes && ./bin/foundry.macos.editor.dev.arm64 --headless --test --test-case="*FoundryCLI*"`
Expected: `Status: SUCCESS!` (these tests should pass immediately; a failure means registry data from Task 2 disagrees with the parser — fix the registry, not the test).

- [ ] **Step 3: Commit**

```bash
git add tests/core/os/test_foundry_cli_help.h
git commit -m "Cross-check CLI help registry against parser"
```

---

### Task 6: Documentation

**Goal:** `doc/tools/foundry_cli.md` documents the help hierarchy, alias, exit codes, and JSON schema.

**Files:**
- Modify: `doc/tools/foundry_cli.md`

**Acceptance Criteria:**
- [ ] All three help levels, the `help` alias, and exit-code rules documented
- [ ] JSON schema example matches the implementation
- [ ] `pre-commit run --all-files` passes

**Verify:** `pre-commit run --all-files` → all hooks pass

**Steps:**

- [ ] **Step 1: Add a "Getting help" section to `doc/tools/foundry_cli.md`**

Insert after the "Global options" section:

```markdown
## Getting help

- `foundry --help` lists the command groups and global options.
- `foundry <command> --help` lists a group's subcommands. `foundry help <command>` is equivalent.
- `foundry <command> <subcommand> --help` documents the subcommand's options,
  positional arguments, and an example invocation.
- Help requests print to stdout and exit 0. An incomplete or unknown command
  prints the relevant scoped help to stderr and exits non-zero.
- Add `--json` for machine-readable help. `foundry --json --help` describes
  every command; `foundry --json script format --help` scopes the output. The
  document carries `foundry_cli_help_version: 1` and a flat `commands` array
  where each entry has `path`, `summary`, `usage`, `availability`
  (`release` or `editor`), `options` (`flag`, `value`, `style`,
  `description`, `required`), `positionals` (`name`, `optional`, `repeats`),
  and `examples`. `style` is `"space"` when the value is a separate token,
  `"equals"` when it is attached as `--flag=value`, and `null` for boolean
  flags. An empty `commands` array means no command compiled into this build
  matches the scope — it does not guarantee the scope itself is valid.

Legacy engine flags are no longer documented in `--help`; the command API
above is the supported surface (see issue #830 for the migration plan).
```

- [ ] **Step 2: Run the lint gate**

Run: `pre-commit run --all-files`
Expected: all hooks pass (fix any formatting complaints it reports).

- [ ] **Step 3: Commit**

```bash
git add doc/tools/foundry_cli.md
git commit -m "docs: document hierarchical Foundry CLI help"
```
