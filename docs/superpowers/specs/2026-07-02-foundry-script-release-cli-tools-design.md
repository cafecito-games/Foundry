# Foundry Script Release CLI Tools Design

**Date:** 2026-07-02
**Status:** Draft pending user review
**Scope:** Make the Foundry Script formatter and a new lint reporter available as supported headless
release-editor CLI features, independent of unit-test builds.

## Goal

Foundry's single shipped binary should support CI/CD workflows for Foundry Script source files without
requiring `tests=yes`. Two command surfaces are in scope:

- `--foundry_script-format`: run the existing canonical formatter against stdin, files, or directories.
- `--foundry_script-lint`: run the real Foundry Script parser/analyzer and emit machine-readable JSON
  or SARIF diagnostics.

Both commands must work from headless release/editor builds where `TESTS_ENABLED` is disabled. They are
not editor UI flows, and they must not require `--test`.

## Non-Goals

- No separate formatter or linter binary in this pass. The product remains the single `foundry`
  executable.
- No external parser, Go parser, or duplicated grammar.
- No broad custom lint-rule framework. The lint command reports parser errors, analyzer errors, and
  existing warning diagnostics when the binary includes them.
- No grammar changes. `modules/foundry_script/GRAMMAR.md` should not need updates for this work.
- `--foundry_script-generate-format-tests` remains a developer/test fixture command unless a later task
  chooses to expose it as a supported release command.

## Build Availability

The formatter implementation currently lives behind `TOOLS_ENABLED` because comment-preserving
formatting uses tokenizer trivia recorded only in tools/editor builds. The supported release target for
these commands is therefore the release editor/headless Foundry binary, not export templates.

Linting can use parser/analyzer diagnostics in release editor builds. Existing `FSWarning` collection is
guarded by `DEBUG_ENABLED`; the first lint implementation should keep the command available regardless
and include warnings only when compiled in. If warning support is absent, the command still reports
parser/analyzer errors and treats warning-specific behavior as having no warning diagnostics.

## Formatter CLI

The existing `FSFormatterCLI` behavior is retained:

```bash
foundry --headless --foundry_script-format [--write|-w|--check|--diff|-d] [paths...|-]
```

- No paths or `-` reads stdin and writes formatted text to stdout.
- File paths are formatted directly.
- Directory paths recurse deterministically for `.fs` files and skip hidden/cache directories.
- `--write` rewrites changed files atomically.
- `--check` prints files that would change and exits `1` when any changes are needed.
- `--diff` prints unified diffs and exits `1` when any changes are needed.
- Parse, read, traversal, or write errors exit nonzero and print diagnostics to stderr.

The root cause to fix is that `main/main.cpp` currently treats `--foundry_script-format` as a test
command in `Main::test_entrypoint()`. In a binary compiled without `TESTS_ENABLED`, this path rejects the
command before normal startup can run. The formatter command should be removed from the test-command
gate and handled as a normal command-line tool during setup/start.

## Lint CLI

Add:

```bash
foundry --headless --path /path/to/project --foundry_script-lint [options] [paths...]
```

Supported options:

- `--format=json`
- `--format=sarif`
- `--out <path>`
- `--fail-on=error`
- `--fail-on=warning`

Defaults:

- Output format: JSON.
- Failure threshold: error.
- Paths: all `.fs` files under the loaded project root when no explicit file or directory paths are
  provided.
- Output destination: stdout unless `--out` is provided.

Explicit paths accept files and directories. Directory traversal should share the formatter's safety
properties: deterministic ordering, `.fs` filtering, hidden directory skipping, and no symlinked
directory descent.

## Diagnostic Model

Create a small lint/reporting module in `modules/foundry_script/`, for example
`fs_lint.{h,cpp}`.

Core structs:

- `FSLintDiagnostic`: path, range, severity, source, rule ID, message.
- `FSLintOptions`: output format, output path, failure threshold, paths.
- `FSLintResult`: diagnostics, had command/config/internal error, exit-code helper.
- `FSLintCLI`: command-line parsing, file collection, output writing, and process exit-code wiring.

Collection flow for each file:

1. Read source text.
2. Run `FSParser::parse(source, path, false)`.
3. If parse fails, emit parser diagnostics from `FSParser::get_errors()` with rule ID `parse-error`.
4. If parse succeeds, run `FSAnalyzer analyzer(&parser); analyzer.analyze()`.
5. Emit analyzer errors from `parser.get_errors()` after analysis with rule ID `analyzer-error`.
6. Under `DEBUG_ENABLED`, emit `parser.get_warnings()` with rule ID `FSWarning::get_name()`.

Diagnostic fields:

- `path`: JSON uses `res://...` when project localization can produce it, otherwise the original path.
- SARIF uses project-relative filesystem paths when possible for GitHub code scanning compatibility.
- `range`: 1-based `startLine`, `startColumn`, `endLine`, `endColumn`.
- `severity`: `error`, `warning`, or `note`.
- `source`: `foundry_script`.
- `ruleId`: `parse-error`, `analyzer-error`, or the warning name.
- `message`: raw parser/analyzer message or warning message.

Best-effort ranges are acceptable where the existing parser only exposes a point. Use the reported line
and column for the start and extend to the end of the source line, matching the LSP diagnostic pattern in
`language_server/fs_extend_parser.cpp`.

## Output Formats

### JSON

Emit stable JSON with sorted dictionary keys and a schema version:

```json
{
	"diagnostics": [
		{
			"message": "The local variable \"value\" is declared but never used.",
			"path": "res://scripts/player.fs",
			"range": {
				"endColumn": 20,
				"endLine": 12,
				"startColumn": 5,
				"startLine": 12
			},
			"ruleId": "unused_variable",
			"severity": "warning",
			"source": "foundry_script"
		}
	],
	"version": 1
}
```

### SARIF

Emit SARIF 2.1.0:

- top-level `version: "2.1.0"`.
- top-level `$schema: "https://json.schemastore.org/sarif-2.1.0.json"`.
- one run with `tool.driver.name: "Foundry Script Lint"`.
- one `rules` entry per emitted rule ID.
- one `results` entry per diagnostic.
- `level` maps `error -> error`, `warning -> warning`, `note -> note`.
- locations use `physicalLocation.artifactLocation.uri` plus 1-based `region` fields.

## Exit Codes

- `0`: no diagnostics at or above the configured failure threshold.
- `1`: diagnostics were found at or above the configured failure threshold.
- `2`: command/config/internal error, such as invalid `--format`, invalid `--fail-on`, unreadable path,
  or failed `--out` write.

`--fail-on=error` fails on errors only. `--fail-on=warning` fails on warnings and errors.

## Main Integration

`main/main.cpp` should recognize both commands as normal command-line tools in `TOOLS_ENABLED &&
MODULE_FOUNDRY_SCRIPT_ENABLED` builds:

- Add help entries for `--foundry_script-format` and `--foundry_script-lint`.
- In setup argument parsing, mark each as `cmdline_tool = true`.
- Force null audio and headless display for both commands unless the user already supplied equivalent
  driver flags.
- Preserve command options and path arguments for parsing by each CLI helper.
- In `Main::start()`, dispatch formatter/lint commands before project game/editor startup.
- Return the command exit code directly.

For lint, `--path` should still load the project before dispatch so `res://`, project settings, strict
analysis settings, autoload indexes, native classes, and resource resolution match real Foundry Script
analysis. If no `--path` is supplied, the usual project discovery from the current working directory
applies.

`Main::test_entrypoint()` should continue to recognize real test commands, but should no longer classify
`--foundry_script-format` as test-only. It may keep `--foundry_script-generate-format-tests` in the test
path.

## Tests

Add focused C++ coverage under `modules/foundry_script/tests/`:

- Formatter command parsing remains covered and no longer depends on test-command registration for the
  production path.
- Lint option parsing handles defaults, invalid `--format`, invalid `--fail-on`, `--out`, and paths.
- Lint file collection recurses deterministically, filters `.fs`, and skips hidden/cache directories.
- JSON serialization includes version, diagnostics, 1-based ranges, severity, source, rule ID, and
  message.
- SARIF serialization includes schema, version, run, tool driver, rules, results, and locations.
- Parser-error lint fixture reports `parse-error`.
- Analyzer-error lint fixture reports `analyzer-error`.
- Warning fixture is covered under `DEBUG_ENABLED`; tests compile out or assert no warning diagnostics
  when warning support is absent.
- Failure threshold behavior returns `0`, `1`, or `2` as specified.

Manual or scripted build verification:

```bash
python3 -m SCons platform=linuxbsd target=editor dev_build=yes tests=no module_text_server_fb_enabled=yes cache_path="$HOME/.scons_cache" -j$(nproc)
./bin/foundry.linuxbsd.editor.dev.x86_64 --headless --foundry_script-format --check modules/foundry_script/tests/scripts/format
./bin/foundry.linuxbsd.editor.dev.x86_64 --headless --path <project> --foundry_script-lint --format=json --fail-on=error
```

Run the existing test-enabled suite after implementation:

```bash
python3 -m SCons platform=linuxbsd target=editor dev_build=yes tests=yes module_text_server_fb_enabled=yes cache_path="$HOME/.scons_cache" -j$(nproc)
./bin/foundry.linuxbsd.editor.dev.x86_64 --headless --test --force-colors
```

## Risks

- Warnings are currently guarded by `DEBUG_ENABLED`; moving them fully into release diagnostics is larger
  than this pass. The CLI should still be present and useful in release builds.
- Analyzer diagnostics may depend on project context. The lint command should prefer running after
  normal project setup rather than as a pre-setup utility.
- SARIF consumers prefer filesystem-relative paths, while Foundry users expect `res://`. The design uses
  JSON for Foundry-native paths and SARIF for code-scanning-friendly paths.
- `main/main.cpp` argument parsing is split across setup/start. Keep the change narrow and mirror
  existing command-line tool patterns such as doctool and migration.
