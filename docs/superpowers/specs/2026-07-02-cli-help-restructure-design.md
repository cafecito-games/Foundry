# Hierarchical CLI Help Design

**Date:** 2026-07-02
**Status:** Approved

## Problem

`foundry --help` prints the command API summary plus every legacy engine option
(General/Run/Display/Debug/Standalone tools) — hundreds of lines that bury the
command-first CLI introduced in #814. There is no per-command help:
`foundry script format --help` is not recognized as a help request.

## Goals

- `foundry --help` lists only the top-level nouns, global options, and a
  pointer to per-command help.
- `foundry <noun> --help` lists that noun's verbs.
- `foundry <noun> <verb> --help` documents that verb's options, positionals,
  and an example.
- Help is also available as machine-readable JSON for agent consumption.
- Legacy engine options disappear from all help output (clean break in
  documentation), while legacy invocations continue to parse.
- Help content cannot drift from the parser (docopt's zero-drift property,
  achieved via a declarative registry plus cross-check tests, not via
  docopt.cpp — see Alternatives).

## Non-Goals (decomposed follow-up project)

- Hard rejection of legacy invocations (`foundry --editor --path .`,
  bare scene paths, `--doctool`, ...). Internal callers (editor self-relaunch,
  export/debugger run flows, CI) still use them.
- Migrating legacy runtime options (`--resolution`, `--rendering-driver`,
  remote debug flags, ...) into documented command options.
- Shell completions and help localization (the registry enables both later).

## Design

### 1. Help UX and hierarchy

Help is triggered by `-h`, `--help`, or `/?` anywhere in a new-CLI invocation,
or by the git-style alias `foundry help [<noun> [<verb>]]`.

**Top level — `foundry --help`:**

- Engine header and copyright lines (unchanged).
- Usage: `foundry <command> <subcommand> [options] [-- user args]`.
- The eight nouns, each with a one-line summary:
  `editor`, `project`, `script`, `test`, `lsp`, `docs`, `extension`,
  `diagnostics`.
- Global options only: `--project <dir>`, `--json`, `--trusted`, `--headless`,
  `--quiet`, `--verbose`, `--no-header`, `--version`, `-h, --help`.
- Pointer line: `Run 'foundry <command> --help' for command details.`

**Noun level — `foundry script --help`:**

- Usage: `foundry script <subcommand> [options]`.
- The noun's verbs with one-line summaries.
- Pointer line to verb-level help.

**Verb level — `foundry script format --help`:**

- Usage line with option/positional placeholders.
- Description, option list with descriptions, positionals, one example
  invocation.

**Behavioral rules:**

- `--help` (and the `help` alias) prints to stdout and exits 0.
- A bare noun (`foundry script`) prints the noun help to stderr and exits 1
  (incomplete command).
- An unknown verb (`foundry script fmt`) prints
  `Unknown script command 'fmt'.` followed by the noun help to stderr,
  exits 1.
- Editor-only commands carry a slim availability badge; the badge legend
  shrinks to at most two lines and appears only in editor builds. Commands
  not compiled into the current build are omitted from text help.
- No legacy option section is printed anywhere.

### 2. JSON help

`--json` combined with a help request emits JSON to stdout (no startup
header, matching the existing `--json` convention):

- `foundry --json --help` — the full command tree.
- `foundry --json script format --help` — only the scoped command(s).

Schema (version 1):

```json
{
  "foundry_cli_help_version": 1,
  "commands": [
    {
      "path": ["script", "format"],
      "summary": "Format Foundry Script files or stdin.",
      "usage": "foundry script format [--project <dir>] [--check|--write|--diff] [paths...]",
      "availability": "release",
      "options": [
        { "flag": "--check", "value": null, "description": "Exit non-zero if changes are needed." },
        { "flag": "--project", "value": "dir", "description": "Project directory." }
      ],
      "positionals": [
        { "name": "paths", "optional": true, "repeats": true }
      ],
      "examples": ["foundry script format --project . --check scripts"]
    }
  ]
}
```

The command list is flat (no nesting) so agents can filter by `path` without
tree traversal. `availability` is one of `release`, `editor`. The version
field increments only on breaking schema changes. Like text help, JSON help
lists only commands compiled into the current build; the `availability` field
describes the minimum build tier, not the running binary.

### 3. Architecture

**New files: `main/cli_help.h`, `main/cli_help.cpp`** — compiled into all
builds, including release export templates (the `script format`/`script lint`
release CLI from #816 must have working help).

- Registry data: `static const` arrays of plain structs holding
  `const char *` fields — no static constructors, no exceptions, no STL.
  - `NounSpec { name, summary }`
  - `CommandSpec { noun, verb, summary, usage_args, availability, options,
    option_count, positionals, example }`
  - `CommandOption { flag, value_name, description }`
- Renderers: `print_top_help()`, `print_noun_help(noun)`,
  `print_command_help(noun, verb)`, `print_help_json(scope)`. Text renderers
  reuse the existing color/padding helpers (`format_help_option` style);
  those helpers move (or are exposed) so `cli_help.cpp` can use them.

**Parser changes (`main/cli_parser.h/.cpp`):**

- `ParseResult` gains `bool help_requested`. When a help flag is seen, the
  parser stops consuming the command, records the deepest command path parsed
  so far in `command_path` (empty = top level), and returns success with
  `help_requested = true`.
- `foundry help [noun [verb]]` is recognized as a help request with the given
  scope.
- The hand-written `parse_*` functions from #814 are otherwise untouched.

**Main changes (`main/main.cpp`):**

- `Main::print_help()` shrinks to a delegation to `print_top_help()`. The
  hardcoded "Command API" block and all legacy option sections
  (General/Run/Display/Debug/Standalone tools) are deleted.
- Early in setup, a `help_requested` parse result dispatches to the scoped
  renderer (text or JSON depending on `--json`) and exits before engine
  initialization.

### 4. Anti-drift tests

`tests/core/os/test_foundry_cli_parser.h` (extended) and a new
`tests/core/os/test_foundry_cli_help.h`, wired into `tests/test_main.cpp`:

- Help detection: `script format --help` yields `help_requested` with scope
  `["script", "format"]`; ditto `-h`, `/?`, the `help` alias, and help flags
  in any argument position; bare-noun and unknown-verb error paths.
- Registry ↔ parser cross-checks:
  - Every `CommandSpec` (noun, verb) is accepted by the parser and dispatches
    to a matching `command_path`.
  - Every noun accepted by `is_new_cli_command()` has a `NounSpec`, and vice
    versa.
  - Every option documented in the registry is accepted by that command's
    parse function without an unknown-option error (fed a dummy value when
    the option takes one).
- JSON help output round-trips through `JSON::parse` and contains the
  schema's required keys.

Known limitation: an option the parser accepts but the registry omits is not
detected mechanically; full bidirectional checking arrives when the follow-up
project makes parsing table-driven.

### 5. Documentation

- `doc/tools/foundry_cli.md`: document the help hierarchy, the `help` alias,
  and the JSON help schema.
- `modules/foundry_script/GRAMMAR.md` is unaffected (no language change).

## Alternatives considered

- **Vendor docopt.cpp** (proposed during brainstorming): rejected because this
  fork builds with `disable_exceptions=True` by default (`-fno-exceptions`)
  and docopt.cpp uses exceptions as API control flow, so it cannot compile
  without an invasive patch set; upstream is unmaintained (last release 2020,
  never reached 1.0), so we would own a fork of a dead library; it drags in
  `std::regex`. Its zero-drift idea is adopted via the registry instead.
- **Hardcoded per-command help printers**: least churn, but help drifts from
  the parser and JSON help would be written twice. Rejected.
- **Full table-driven parser** (registry drives parsing too): cleanest
  end-state but requires rewriting the 900-line parser now; deferred to the
  legacy-removal follow-up, which the registry sets up.

## Migration strategy

The #814 driver already migrates in slices (`is_new_cli_command()` gates on
the first token; everything else falls through to legacy parsing). This
design rides that: new commands added later must add a registry entry or the
cross-check tests fail, keeping help authoritative slice by slice.
