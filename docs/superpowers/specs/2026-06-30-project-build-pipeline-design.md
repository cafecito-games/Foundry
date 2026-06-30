# Project Build Pipeline Design

Date: 2026-06-30

## Summary

Foundry projects need a first-class build pipeline that can run project-defined work before and after script
compilation. The motivating case is generated code: a project may use protobuf or a similar schema tool, generate
Foundry Script source files, then compile and index scripts that depend on those generated files.

The pipeline must be shared by the editor, Foundry Script LSP, run/export flows, and CLI/test flows. A failed required
pre-compile task blocks downstream script compilation and indexing. Foundry must not silently continue with stale
generated outputs.

## Goals

- Define build stages in `project.foundry`, the project authority.
- Support ordered `pre_compile` and `post_compile` task lists.
- Support both project-authored Foundry Script providers and command-style tasks.
- Let providers come from engine modules, addons, or explicit project registrations.
- Provide an editor authoring UI for enabling the pipeline, adding tasks, reordering stages, editing task fields, and
  validating configuration before it is saved.
- Gate editor indexing, LSP workspace loading, run/export, and tests on the same pipeline state.
- Re-run tasks only when their inputs, provider scripts, config, or outputs change.
- Require an explicit trust decision before project-authored build code or external commands run automatically.

## Non-Goals

- A full dependency graph between tasks. The first version uses ordered stage lists.
- A separate virtual source root for generated code. Generated source files live in visible `res://` paths.
- A bundled protobuf provider in the first slice. Protobuf can be an example or follow-up provider.
- A visual graph editor for build tasks. Ordered list editing is enough for the first implementation.
- Annotation-only provider discovery. `@build_task` is useful for tooling and validation, but provider availability
  comes from registration metadata so the pipeline does not have to index the whole project before `pre_compile`.

## Architecture

Add a first-class service tentatively named `ProjectBuildPipeline`. It loads build settings from `project.foundry`,
validates stage order and task definitions, resolves each task to a provider, and exposes a main operation:

```text
ensure_built(stage)
```

Callers use the service instead of running generator-specific logic:

- Editor startup asks for `pre_compile` before the first filesystem and script index pass.
- `FSWorkspace::initialize()` asks for or observes `pre_compile` before `reload_all_workspace_scripts()`.
- Run/export/test paths ask for `pre_compile`, run existing compilation/export/test logic, then ask for `post_compile`
  when configured.
- File watching marks affected tasks dirty and asks the pipeline to re-evaluate them. Downstream indexing and
  compilation wait while required pre-compile work is pending.

The service exposes explicit states:

- `disabled`: no enabled build pipeline is configured.
- `untrusted`: project build tasks exist but the user has not trusted them.
- `clean`: requested stage outputs are current.
- `dirty`: one or more tasks must be re-evaluated.
- `running`: a stage is currently executing.
- `blocked`: a required task failed or validation failed.

`blocked` prevents downstream compilation and indexing until the failing task succeeds, is disabled, or the project
configuration changes so it is no longer required.

## Project Configuration

The build definition lives under a `build` namespace in `project.foundry`. The stage lists are ordered arrays of task
names, and each task has its own configuration block.

```ini
[build]
enabled=true
pre_compile=PackedStringArray("generate_proto", "post_process_proto")
post_compile=PackedStringArray("bundle_metadata")

[build/providers/generate_protobuf]
script="res://addons/protobuf_build/generate_protobuf.fs"
class_name="GenerateProtobuf"

[build/tasks/generate_proto]
provider="generate_protobuf"
inputs=PackedStringArray("res://proto/**/*.proto")
outputs=PackedStringArray("res://generated/protobuf/")
options={
"language": "foundry_script",
"package": "game.net"
}

[build/tasks/post_process_proto]
provider="command"
command="python3"
args=PackedStringArray("tools/postprocess_proto.py", "res://generated/protobuf")
inputs=PackedStringArray("res://generated/protobuf/**/*.fs")
outputs=PackedStringArray("res://generated/protobuf/")
```

Task fields:

- `provider`: required provider ID resolved through the build task registry.
- `inputs`: declared input files or globs.
- `outputs`: declared output files, roots, or globs.
- `options`: provider-specific dictionary.
- `command` and `args`: required only when `provider="command"`.
- `working_directory`: optional, defaults to the project root.
- `environment`: optional declared environment additions.
- `timeout_seconds`: optional, with a conservative default.
- `enabled`: optional per-task toggle.

Generated source outputs should live in visible `res://` locations such as `res://generated/` or `res://gen/`.
Non-source caches, fingerprints, stdout/stderr logs, and provider intermediates live under the project data path,
outside `res://`.

Provider descriptor fields:

- `script`: Foundry Script file that defines the task provider class.
- `class_name`: provider class to instantiate from the script.
- `display_name`: optional UI label.
- `description`: optional UI description.
- `addon`: optional addon/source identifier for diagnostics and collision reporting.

Engine-provided providers, such as the reserved `command` provider, register native descriptors directly and do not need
a script descriptor.

## Provider Registration Model

Providers are registered into a `FoundryBuildTaskRegistry`. Tasks reference provider IDs from that registry.

Provider sources:

- Engine modules register built-in providers directly, for example `command` and future first-party providers.
- Addons register providers through addon metadata. This lets external libraries expose build tasks without asking the
  project to copy files into a special folder.
- Projects register additional providers under `build/providers/*` in `project.foundry`.

Addon metadata uses the same descriptor shape as project provider registrations. For example:

```ini
[build_tasks]
providers=PackedStringArray("protobuf.generate")

[build_tasks/protobuf.generate]
script="res://addons/protobuf_build/generate_protobuf.fs"
class_name="GenerateProtobuf"
display_name="Generate Protobuf"
```

A provider script imports the built-in build task API and extends `FoundryBuildTask`.

```fs
namespace my.game.build_tasks

import foundry.build_tasks

@build_task
class_name ProtobufGenerationProvider extends FoundryBuildTask:
	func command(p_context: FoundryBuildContext) -> FoundryBuildCommand:
		return FoundryBuildCommand.new(
			command=["python3", "protobuf/codegen.py", "-w", p_context.current_working_directory],
		)
```

`@build_task` is a tooling and validation annotation. It helps the editor identify build-task classes, offer
completion, and warn when a registered class is missing the expected marker. It is not the discovery mechanism by
itself, because annotation-only discovery would require indexing arbitrary project scripts before the `pre_compile`
stage has run.

`FoundryCommandBuildTask` can exist as a helper base class or trait for the common "return a command" case. More
complex providers can implement `run(context)` directly, emit structured diagnostics, run multiple commands, or write
an output manifest.

Because `pre_compile` providers must run before normal project script compilation and indexing, provider scripts are a
bootstrapped island:

- They may import the built-in `foundry.build_tasks` API.
- They may import helper scripts that live next to the registered provider script or under the same addon.
- They must not depend on generated project code, autoloads, game scripts, or the normal project symbol graph.
- The pipeline loads them through a restricted build task script loader before normal indexing.

The trust prompt applies to script-backed project and addon providers, not only raw command execution. Provider code can
trigger external work, so the editor must treat it as trusted project code.

The reserved `command` provider is built in. It exists as a low-friction escape hatch for projects that only need to
run an executable with declared inputs and outputs.

## Built-In Build Task API

The built-in API should be small and stable:

- `FoundryBuildTask`: base class for project providers.
- `FoundryBuildContext`: immutable task context containing project root, working directory, task name, stage, inputs,
  outputs, options, environment, project data path, and a diagnostic sink.
- `FoundryBuildResult`: success/failure result with diagnostics and optional output manifest.
- `FoundryBuildCommand`: command description with executable, arguments, environment, working directory, timeout, and
  stdout/stderr capture settings.
- `FoundryBuildTaskConfigSchema`: optional provider-supplied schema metadata for the authoring UI. This describes
  provider options, default values, labels, hints, required fields, and validation rules.
- `FoundryCommandBuildTask`: optional helper for providers that only need to return one or more commands.

The API should avoid exposing broad editor internals. Providers should receive the information needed to generate
outputs and report diagnostics, but the pipeline should retain control of trust, process execution, output capture,
and write-scope validation.

Providers can optionally expose a configuration schema method, for example `get_config_schema()`, so the editor can
render provider-specific options without requiring hand-authored dictionaries. Providers that do not expose a schema
still work, but the UI falls back to a generic dictionary editor for `options`.

## Execution Flow

For a requested stage, the pipeline:

1. Loads and validates the `project.foundry` build config.
2. Checks trust. If project build tasks are not trusted, the pipeline enters `untrusted` and does not execute providers.
3. Builds the provider registry from native providers, enabled addon metadata, and `project.foundry` provider
   descriptors.
4. Bootstraps script-backed providers with the restricted build task loader.
5. Validates every task in the requested stage before running any task:
   - provider exists,
   - inputs are readable or valid globs,
   - outputs are declared,
   - output roots do not overlap unsafe paths,
   - command tasks have valid command data,
   - script-backed provider descriptors point to project files.
6. Computes fingerprints and skips clean tasks.
7. Runs dirty tasks sequentially in configured order.
8. Stops on the first failure and marks the project `blocked`.
9. After a successful task that changes `res://` outputs, asks `EditorFileSystem` to scan changes.
10. Marks the requested stage clean only after all required tasks succeed or are skipped as clean.

`pre_compile` is blocking for editor indexing, LSP workspace loading, script compilation, run, export, and tests.
`post_compile` runs after successful compilation. It blocks completion of run/export/test flows that requested it, but
does not gate editor indexing unless a caller explicitly requests that stage.

## Invalidation

File watching is a trigger, not the source of truth. The pipeline persists a content fingerprint for each task under
the project data path.

The fingerprint includes:

- normalized task configuration from `project.foundry`,
- provider descriptor metadata,
- provider script contents,
- imported helper scripts used by script-backed providers,
- declared input file contents,
- declared environment and options,
- provider API version,
- tool identity when a provider can report one,
- previous output manifest.

A task is dirty when:

- its fingerprint changes,
- any declared output is missing,
- a previous run failed,
- a provider requests a forced rerun,
- the persisted build state is missing or unreadable.

Watch triggers include:

- `project.foundry`,
- enabled addon build-task metadata,
- registered provider scripts and their helper imports,
- declared inputs,
- declared outputs.

The editor debounces reruns so a save operation or branch switch does not launch repeated generator processes.

## Diagnostics And Failure Handling

Build diagnostics are structured and visible to all callers. A failing task records:

- stage and task name,
- provider ID and descriptor source,
- command line when applicable,
- process exit code when applicable,
- stdout/stderr tail,
- provider-emitted diagnostics,
- file/line information when available.

While blocked, the LSP should report build diagnostics instead of stale script diagnostics. The editor should show a
compact build status indicator and a build output panel. The project remains blocked until the task succeeds, is
disabled, or the build config changes.

## Editor And LSP Integration

Editor startup:

- Load `project.foundry`.
- If build tasks exist and the project is not trusted, pause execution and show a trust prompt/status.
- If trusted, run `pre_compile`.
- Run `EditorFileSystem` scan and Foundry Script indexing only after `pre_compile` succeeds.

LSP:

- `FSWorkspace::initialize()` waits for or observes the pre-compile gate before `reload_all_workspace_scripts()`.
- If pre-compile is blocked, the workspace publishes build diagnostics.
- When a successful pre-compile changes generated `.fs` files, the workspace reloads after the filesystem scan catches
  up.

Run/export/test:

- Ask `ProjectBuildPipeline` to ensure `pre_compile` before script compilation.
- Run the existing flow.
- Ask `ProjectBuildPipeline` to run `post_compile` after successful compilation when configured.

Generated files:

- Declared generated source roots are visible under `res://`.
- The editor can warn before editing files under declared generated roots.
- Fingerprint state and non-source intermediates stay outside `res://`.

## Build Configuration UI

The first implementation includes a dedicated authoring UI, preferably as a Project Settings page or adjacent project
configuration panel. Hand-editing `project.foundry` remains possible, but should not be the expected workflow.

The UI should provide:

- a global build pipeline enable/disable toggle,
- separate ordered lists for `pre_compile` and `post_compile`,
- add, duplicate, remove, enable/disable, and reorder controls for tasks,
- a task details editor for name, provider, inputs, outputs, working directory, environment, timeout, and options,
- a provider selector that lists built-in, addon-provided, and project-registered providers,
- command-specific fields when `provider="command"`,
- provider-specific option controls when a trusted provider exposes schema metadata,
- a generic dictionary editor fallback for provider options without schema metadata,
- validation messages before saving,
- a preview of the `project.foundry` sections that will be written,
- actions to run a selected task, run a stage, and clear cached build state.

The UI writes back to `project.foundry` using `ProjectSettings`/`ConfigFile` conventions so it preserves the project as
the source of truth. Reordering in the UI updates the ordered stage arrays. Creating a project-authored provider from
the UI can scaffold a provider script at a user-selected project path and add a matching `build/providers/*`
registration.

Provider discovery for the selector must be descriptor-based and non-executing, so untrusted projects can still be
inspected and edited. Provider-supplied schemas require loading provider code and are therefore available only after
the project build tasks are trusted. Before trust, script-backed providers use the generic options editor.

Validation in the UI should mirror pipeline validation:

- duplicate task names are rejected,
- stage entries must reference task definitions; disabled tasks remain visible in the ordered stage list but are skipped
  during execution,
- provider names must resolve to a built-in provider, addon provider, or project provider descriptor,
- command tasks must have a command,
- inputs and outputs must be valid project paths or globs,
- generated output roots must not target unsafe locations such as `res://` itself,
- provider schema validation errors are shown inline.

The UI should also surface trust state. If a project is untrusted, task editing is still allowed, but run buttons are
disabled until the user trusts project build tasks.

## Security And Trust

Script-backed providers from projects or addons, and command tasks that can execute external processes, require
explicit trust before automatic execution in the editor. The trust decision should be stored outside the project file so
it is local to the developer machine.

Before running a task, the pipeline validates declared output roots. Providers should not be allowed to write arbitrary
project files without declaring them as outputs. The first implementation can enforce this best for command-style tasks
by checking changed output manifests after execution; deeper write interception can be a later hardening step.

CLI use should have an explicit flag or environment policy for trusted execution so CI can opt in intentionally.

## Testing

Tests should cover:

- `project.foundry` build config parsing,
- ordered stage execution,
- provider registry construction from native, addon, and project descriptors,
- provider bootstrap restrictions,
- trust blocking,
- command construction and timeout handling,
- build configuration UI model validation and serialization,
- provider schema metadata rendering/fallback behavior where practical,
- fingerprint invalidation,
- output-missing invalidation,
- failure blocking,
- editor/LSP pre-compile gating where practical.

Foundry Script fixtures should cover simple provider loading, command-provider behavior, diagnostics, and invalid
provider dependencies.

## Open Follow-Ups

- Add a bundled protobuf provider or documented example.
- Add richer provider conflict-resolution UI if multiple addons or project descriptors register the same provider ID.
- Add stronger write-scope enforcement for providers that perform file writes directly.
- Add richer watch glob semantics if the initial filesystem integration needs it.
