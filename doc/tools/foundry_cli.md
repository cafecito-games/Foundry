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

## Commands

```sh
foundry editor open --project .
foundry editor project-manager

foundry project run --project . --scene res://main.tscn -- --game-arg value
foundry project export --project . --preset Linux --output build/game.x86_64 --mode release
foundry project import --project .

foundry script format --project . --check scripts
foundry script lint --project . --format=sarif --out reports/foundry-script.sarif scripts
foundry script migrate --trusted --project . --apply --strict null,dynamic --confirm

foundry test run --project . --case "*FoundryScript*"
foundry lsp serve --project . --port 6005

foundry docs generate-api --include-docs
foundry docs generate-engine --output doc-out
foundry extension dump-interface --format json
foundry diagnostics render-device-support
```

## Argument boundary

Foundry parses command arguments before `--`. Everything after `--` is reserved
for the project and is exposed through `OS.get_cmdline_user_args()`.
