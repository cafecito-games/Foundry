# Foundry command line

Foundry supports a command-first CLI for editor, project, script, test, language
server, documentation, extension, and diagnostic workflows. The command API is
designed for humans and agents: commands are grouped by noun, project code
execution is explicit, and project/user arguments are passed only after `--`.

## Global options

- `--project <dir>` selects a project directory containing `project.foundry`.
- `--json` requests machine-readable output where a command supports it and
  suppresses the startup header.
- `--trusted` allows project-defined build task execution for commands that can
  run project code.
- `--headless`, `--quiet`, `--verbose`, `--no-header`, driver, rendering, and
  logging options keep their engine meanings.

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

Legacy workflow flags such as `--editor`, `--path`, `--test`, and
`--foundry_script-format` are rejected with a replacement hint. Use the
command-first invocations documented above.

## Commands

```sh
foundry editor open --project .
foundry editor project-manager

foundry project run --project . --scene res://main.tscn -- --game-arg value
foundry project test --project . --runner res://addons/foundrylib/testlib/cli/run.fs -- --path res://tests
foundry project export --project . --preset Linux --output build/game.x86_64 --mode release
foundry project import --project .

foundry script format --project . --check scripts
foundry script lint --project . --format=sarif --out reports/foundry-script.sarif scripts
foundry script migrate --trusted --project . --apply --strict null,dynamic --confirm
foundry --headless script eval 'print(Engine.get_version_info()["string"])'

foundry test run --project . --case "*FoundryScript*"
foundry test generate-fixtures modules/foundry_script/tests/scripts
foundry test generate-format-fixtures modules/foundry_script/tests/scripts/format
foundry lsp serve --project . --port 6005

foundry docs generate-api --include-docs
foundry docs generate-engine --output doc-out
foundry extension dump-interface --format json
foundry diagnostics render-device-support
```

## Inline script evaluation

`foundry script eval <source>` runs a short inline Foundry Script snippet without
creating a temporary `.fs` file. The snippet becomes the body of a generated
`ScriptRunner.run(args)` override, so it executes under a live `SceneTree` main
loop and the process exit code follows the runner contract:

- A snippet with no explicit `return` exits `0`.
- `return <int>` sets the process exit code (for example `script eval 'return 3'`
  exits `3`).
- Parse, analysis, compile, or runtime errors exit non-zero.

The snippet may reference `args` (a `PackedStringArray`) to read user arguments
passed after `--`. `--project <dir>` runs the snippet in a project's context so
project `class_name` scripts and `ClassDB` are visible; a project is not required.
This replaces the legacy `--script -e` form, which is not supported.

```sh
foundry --headless script eval 'print("ok")'
foundry --headless script eval --project . 'print(ClassDB.class_exists("Node"))'
foundry --headless script eval 'print(args)' -- --some-user-arg
```

## Argument boundary

Foundry parses command arguments before `--`. Everything after `--` is reserved
for the project and is exposed through `OS.get_cmdline_user_args()`.
