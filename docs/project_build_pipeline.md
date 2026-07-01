# Project Build Pipeline

Foundry projects can define a build pipeline in `project.foundry` for work that must happen before or after script
compilation. The pipeline is intended for generated source workflows: schema files, content manifests, or other project
inputs can produce `.fs` files under `res://generated/`, then the editor, LSP, run, export, and CLI test flows all use
the same build state.

## `project.foundry` Sections

Build settings live under `[build]`, `[build/tasks/<name>]`, and `[build/providers/<id>]`.

```ini
[build]
enabled=true
pre_compile=PackedStringArray("generate_protobuf")
post_compile=PackedStringArray("verify_generated_manifest")

[build/tasks/generate_protobuf]
provider="command"
command="foundryproto"
args=PackedStringArray(
	"--input", "proto/player.proto",
	"--output", "generated/protobuf/",
	"--package", "game.net"
)
inputs=PackedStringArray(
	"res://proto/**/*.proto"
)
outputs=PackedStringArray(
	"res://generated/protobuf/"
)
working_directory="res://"
tool_version_command=PackedStringArray("foundryproto", "--version")
timeout_seconds=60

[build/tasks/verify_generated_manifest]
provider="command"
command="python3"
args=PackedStringArray("tools/verify_generated_manifest.py", "res://generated/protobuf/")
inputs=PackedStringArray("res://generated/protobuf/**/*.fs")
outputs=PackedStringArray("res://build/protobuf_manifest.txt")
working_directory="res://"
```

`pre_compile` and `post_compile` are ordered task lists. Disabled tasks remain visible in the ordered lists but are
skipped during execution.

Task fields:

- `provider`: required provider id. `command` is the built-in external process provider.
- `command` and `args`: command-provider executable and argv entries. Args are not shell-expanded.
- `inputs`: project files or globs that participate in dirty detection.
- `outputs`: generated files or roots. Tasks must declare at least one output.
- `working_directory`: optional `res://` directory for command execution, defaulting to the project root.
- `environment`: optional dictionary of environment additions.
- `tool_version_command`: optional argv used to fingerprint external tool versions.
- `timeout_seconds`: optional positive timeout.
- `enabled`: optional per-task toggle.
- `options`: provider-specific dictionary for non-command providers.

Project and addon providers are registered with descriptor sections:

```ini
[build/providers/protobuf.generate]
script="res://addons/protobuf_build/generate_protobuf.fs"
class_name="GenerateProtobuf"
display_name="Generate Protobuf"
description="Generates Foundry Script protobuf bindings."
```

Provider discovery is descriptor-based. The editor can show configured providers before trusting project build code;
provider-specific option schemas are loaded only after trust.

## Generated Output Layout

Generated `.fs` sources should live in visible project paths such as `res://generated/` or `res://gen/`. Declare those
roots in `outputs` so Foundry can rescan and reindex them after a successful pre-compile task.

Keep caches, temporary files, stdout/stderr logs, and non-source intermediates outside generated source roots. Foundry
stores its persisted task fingerprints and trust state outside the project file so they are local to the developer
machine.

## Editor Authoring

Use the Build Pipeline settings dialog from Project Settings to enable the pipeline, add tasks, reorder stage lists,
edit command fields, and preview the `project.foundry` sections before saving. Hand-editing remains supported, but the
dialog runs the same validation rules as the pipeline:

- stage entries must reference task definitions,
- provider ids must resolve to built-in, addon, or project descriptors,
- command tasks must declare a command,
- inputs and outputs must be `res://` paths or globs,
- output roots cannot target unsafe locations such as `res://`, `.foundry`, or `.godot`,
- run actions are disabled until the project build tasks are trusted.

## Trust And CLI Use

External commands and script-backed providers require trusted execution. In the editor, untrusted build tasks block
downstream indexing and compilation until the project is trusted or the task is disabled. LSP diagnostics report the
build blocker instead of indexing stale generated output.

Headless CLI flows use the same stages. CI should opt into trusted execution only for repositories it controls, then run
the usual command:

```sh
./bin/foundry.linuxbsd.editor.dev.x86_64 --headless --test --force-colors
```

If a required `pre_compile` task fails, run, export, test, and script-check flows stop before using stale outputs. A
configured `post_compile` stage runs only after the main flow succeeds.
