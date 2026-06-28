# Foundry Script Autoload Index And Script-Owned Autoloads Design

Date: 2026-06-27
Status: Runtime/export migration in progress
Fork: CafecitoGames / Godot Engine

## Purpose

Godot autoloads are currently owned by `project.foundry` settings under
`autoload/<Name>`. That makes autoload declarations live outside the scripts they
instantiate, and it forces Foundry Script analysis, editor completion, LSP lookups, doc
generation, and runtime startup to ask project settings directly.

The long-term goal is to let scripts declare that they are autoloads:

```gdscript
@autoload(depends_on = [SaveManager])
class_name EventBus extends Node
```

The script should eventually be the source of truth, and project settings
autoload configuration should become a compatibility/migration path rather than
the primary authoring model.

The safe first step is not the annotation. The safe first step is an
analyzer-backed autoload index that centralizes autoload metadata while current
project settings still define the actual autoload list. Once all consumers use
the index, adding `@autoload` becomes a producer change instead of a cross-engine
rewrite.

## Goals

- Add a first-class autoload index API that describes project autoloads without
  every consumer reading `ProjectSettings::get_autoload_list()` directly.
- Seed the first implementation from existing project settings, preserving
  runtime behavior and editor-visible ordering.
- Resolve each autoload to script-aware metadata when possible: name, path,
  singleton/global flag, order, root script class, native base, `Node`
  validity, and diagnostics.
- Make Foundry Script analyzer/editor/LSP code consume the index for autoload lookup
  and type metadata.
- Support the intentional dual identity needed by future script-owned autoloads:
  the same name can denote a type in type positions and a singleton instance in
  value positions when the name belongs to that autoload's own script.
- Provide a single place for future ordering and dependency validation.
- Prepare for `@autoload(depends_on = [OtherAutoload])` where dependencies are
  class symbols, not fragile strings.
- Keep the first milestone behavior-compatible with existing projects.

## Non-Goals

- Implement `@autoload` in the first milestone.
- Remove or rewrite `project.foundry` autoload settings in the first milestone.
- Change runtime autoload startup order in the first milestone.
- Add a new `autoload` Foundry Script keyword.
- Scan every script at game startup.
- Require full analyzer dependency resolution during the existing global-class
  metadata scan.
- Replace editor UI behavior in the first milestone, beyond surfacing index
  diagnostics where useful.

## Current Architecture

Autoload configuration is represented by project settings:

```text
autoload/EventBus = "*res://event_bus.fs"
```

`ProjectSettings::_set()` turns these settings into
`ProjectSettings::AutoloadInfo` entries. The leading `*` marks the autoload as a
singleton/global constant.

Runtime startup in `main.cpp` reads
`ProjectSettings::get_autoload_list()`, seeds singleton globals with `Nil`,
loads each scene or script, validates script resources inherit from `Node`, sets
the node name, registers singleton globals, and adds the nodes to the root
viewport.

The editor's first filesystem scan discovers global script classes before it
creates autoload scripts. This is important because autoload scripts can refer to
global classes. Exported projects already rely on saved script metadata, such as
`global_script_class_cache.cfg`, because they cannot reconstruct all global
class metadata by scanning scripts at runtime.

Foundry Script analyzer and editor code currently read project settings directly in
several places to resolve autoload singleton names, type autoload references, and
navigate to autoload symbols. The analyzer also rejects `class_name X` when an
autoload singleton named `X` already exists. That rule is correct for today's
manual configuration, but it conflicts with script-owned autoloads where the
script must be both `class_name EventBus` and the source of the `EventBus`
singleton instance.

## Design Summary

Introduce an autoload index as a shared metadata layer:

```text
ProjectSettings autoloads
        |
        v
Autoload index builder
        |
        v
Autoload index entries
        |
        +--> analyzer name/type resolution
        +--> editor completion/navigation/highlighting
        +--> LSP symbol and type support
        +--> docgen/test-runner startup parity
        +--> future dependency/order diagnostics
        +--> future runtime startup source
```

V1 is project-settings-backed and read-only:

- Project settings remain the only producer.
- Runtime startup remains behavior-compatible.
- The index records the same order project settings expose today.
- Consumers gradually move from ad-hoc `ProjectSettings` reads to the index.

Future milestones add a second producer:

```text
@autoload annotation scan/analyzer
        |
        v
Autoload index builder
```

Once the script producer is mature, project settings can become a compatibility
producer and eventually be retired.

## Autoload Index Data Model

The index entry should be explicit enough that consumers do not need to reload
project settings or re-derive basic facts.

```cpp
struct GDScriptAutoloadIndexEntry {
	StringName name;
	String path;
	bool is_singleton = true;
	int order = 0;

	// Source of the entry.
	enum Source {
		SOURCE_PROJECT_SETTINGS,
		SOURCE_SCRIPT_ANNOTATION,
	};
	Source source = SOURCE_PROJECT_SETTINGS;

	// Script-aware metadata, populated when the path points at a script or when a
	// scene instance exposes a script.
	StringName global_class_name;
	String script_path;
	StringName native_base;
	bool is_node = false;
	bool is_tool = false;

	// Diagnostics collected while indexing. These should be stable enough for
	// editor surfaces and tests, but consumers may also ask for structured codes.
	Vector<AutoloadDiagnostic> diagnostics;
};
```

V1 can keep the implementation narrower than this shape if needed, but the API
should avoid forcing callers to reach back into `ProjectSettings` for name,
path, singleton state, order, or basic validity.

Suggested queries:

```cpp
bool has_autoload(const StringName &p_name) const;
const AutoloadIndexEntry *get_by_name(const StringName &p_name) const;
const AutoloadIndexEntry *get_by_path(const String &p_path) const;
const AutoloadIndexEntry *get_by_global_class(const StringName &p_class) const;
void get_all(Vector<AutoloadIndexEntry> &r_entries) const;
uint64_t get_version() const;
```

The version increments whenever index content changes. Analyzer/editor caches can
use it for invalidation.

## Index Ownership And Placement

The index should live close to Foundry Script language/editor infrastructure, not in
`ProjectSettings`. `ProjectSettings` should remain a producer of raw autoload
configuration, not the semantic owner of Foundry Script-specific type metadata.

Reasonable placement:

- `modules/foundry_script/gdscript_autoload_index.{h,cpp}` for language-facing
  metadata and analyzer queries.
- Editor-only hooks in `modules/foundry_script/editor/` if the index needs richer
  editor diagnostics or filesystem integration.
- Thin integration from `ProjectSettings` settings-changed events to refresh or
  invalidate the index.

The index should be language-aware enough to handle Foundry Script entries well, but it
must not prevent other script languages from continuing to work through the
existing autoload path. V1 may limit script-aware metadata to Foundry Script while
preserving generic autoload name/path/singleton/order entries for all autoloads.

## V1: Project-Settings-Backed Read-Only Index

V1 builds from `ProjectSettings::get_autoload_list()` and preserves current
behavior.

For each entry:

1. Copy name, path, singleton flag, and project-settings order.
2. Resolve UID paths with the same rules startup/editor code use.
3. If the path is a Foundry Script resource, parse enough metadata to identify:
   - root `class_name`, including namespace-qualified names;
   - native base, using the same tolerant global-class extraction path where
     possible;
   - whether the script ultimately inherits `Node`.
4. If the path is a `PackedScene`, record basic path/type metadata in V1. Scene
   script extraction can be added later because it is more expensive and depends
   on resource loading.
5. Record diagnostics instead of failing the whole index.

V1 diagnostics:

- Missing path.
- Path is neither script nor scene.
- Script does not inherit `Node`.
- Script parse failed enough that class/base metadata could not be recovered.
- Singleton name collides with a language reserved global name.
- Autoload name collides with an unrelated global class.
- Autoload name matches its own script `class_name`, recorded as intentional and
  allowed by future analyzer rules.

## Analyzer Integration

The analyzer should ask the index for autoload symbols instead of reading project
settings directly.

Value-position lookup:

- A singleton autoload name resolves to the singleton instance type.
- If the backing script can be analyzed, use the script's class type.
- If the backing script cannot be analyzed, fall back to `Node`, matching current
  behavior.

Type-position lookup:

- A global class remains a type.
- An autoload singleton must not be used as a base type merely because a global
  constant has the same name.
- If an autoload's name matches its own script `class_name`, type positions bind
  to the class and value positions bind to the singleton instance.
- If an unrelated project autoload and unrelated global class share a name, keep
  reporting a conflict rather than silently choosing one.

Class-name validation:

- Keep rejecting `class_name X` that hides an unrelated singleton autoload `X`.
- Stop rejecting `class_name X` when the index says autoload `X` is backed by the
  same script path. This is the key compatibility step for future script-owned
  autoloads.

This preserves today's safety rule while allowing the model that script-owned
autoloads require.

## Editor, LSP, And Tooling Integration

Move existing editor consumers to the index in stages:

- completion lists for autoload singleton globals;
- syntax highlighting for autoload names;
- go-to-definition for autoload names;
- script editor symbol lookup;
- LSP document symbols, definitions, hover/type surfaces where autoloads appear;
- docgen behavior that currently reads autoload settings;
- Foundry Script test runner startup parity.

The first change should be consumer-neutral: outputs should match current
project-settings behavior. Tests should prove the index is not changing name
resolution, navigation, or startup behavior.

## Future `@autoload` Surface

The preferred long-term syntax is an annotation on the root script:

```gdscript
@autoload
class_name EventBus extends Node
```

With dependency support:

```gdscript
@autoload(depends_on = [SaveManager])
class_name EventBus extends Node
```

And optional manual ordering:

```gdscript
@autoload(order_id = Autoloads.EVENT_BUS)
class_name EventBus extends Node
```

`class_name` should be required. It gives the autoload its public singleton name
and type name without introducing a second naming surface.

`extends Node` or an inherited `Node` base should be required. Autoloads are
instantiated and added to the scene tree.

Named built-in annotation arguments can be implemented either generally or as an
`@autoload`-specific parser/analyzer path. The general feature is larger because
all built-in annotations would need named-argument binding, diagnostics,
completion, docs, and tests. An `@autoload`-specific path is acceptable if it
keeps the language surface contained while the feature is experimental.

## Future Dependency And Ordering Semantics

Autoload order matters because autoloads are instantiated and added to the scene
tree in order. That can affect `_enter_tree()`, `_ready()`, root child order,
process order among equal priorities, and scripts that connect to or initialize
other autoloads during startup.

The future script-owned model should sort autoloads with this algorithm:

1. Build entries from all `@autoload` scripts.
2. Validate all entries have unique names and inherit `Node`.
3. Resolve `depends_on` entries as class symbols that point to other autoloads.
4. Topologically sort dependencies.
5. Within otherwise unconstrained groups, sort by `order_id` if supplied.
6. Break remaining ties by stable path/name order.

This makes dependencies express correctness while `order_id` remains a manual
preference/tie-breaker.

Cycle diagnostics should be hard errors:

```text
Autoload dependency cycle: EventBus -> SaveManager -> EventBus.
```

Missing or non-autoload dependency diagnostics should also be hard errors:

```text
Autoload "EventBus" depends on "SaveManager", but "SaveManager" is not an autoload.
```

## Analyzer-Backed Annotation Indexing

Supporting `depends_on = [SaveManager]` well requires analyzer-backed indexing.
The early global-class scan is intentionally tolerant and avoids full analyzer
dependency resolution. That scan is not the right place to fully evaluate class
symbols or constants from other scripts.

The future annotation producer should have two phases:

1. A scan phase records candidate `@autoload` scripts and raw annotation
   argument syntax.
2. An analyzer-backed phase resolves class-symbol dependencies and constant
   `order_id` expressions, validates them, and writes stable metadata into the
   autoload index/cache.

This avoids starting with string-only dependencies while still preserving export
support. Exported projects should use saved/indexed metadata rather than scanning
every script at startup.

## Editor Autoload View

The existing Autoload project settings page should eventually become an index
view and migration surface.

Near-term:

- Show current project-settings autoloads as index entries.
- Surface index diagnostics.
- Continue editing project settings.

Mid-term:

- Show both project-settings and script-owned entries.
- Mark source for each entry.
- Warn on conflicts.
- Allow drag ordering only for entries whose source supports manual ordering.

Long-term:

- Script-owned entries are primary.
- Project-settings entries are compatibility/migration warnings.
- Drag ordering writes source-owned metadata where possible, such as
  `order_id` constants in a generated `Autoloads.fs` ordering file.

The generated ordering file idea is viable:

```gdscript
class_name Autoloads extends RefCounted

const EVENT_BUS = 10
const SAVE_MANAGER = 20
```

Gaps allow insertions without rewriting every value. This should be treated as a
future editor-authoring convenience, not a V1 requirement.

## Runtime Migration Path

Runtime startup now reads the autoload index. Editor/tools builds rebuild the
index from project settings and script annotations before local project runs and
refresh the saved cache. Export/runtime builds read saved script-owned entries
from the cache and merge current project settings afterward as compatibility
input, so exported projects do not scan every script to discover script-owned
autoloads at startup and stale cached project-settings entries cannot override
the exported settings or runtime overrides.

Index-backed startup registers the selected startup entries back into the
process-local `ProjectSettings` autoload list before scripts compile. This keeps
existing compiler/runtime singleton deferral behavior compatible while the
autoload index becomes the startup source.

Project-settings order remains compatibility input. `autoload_prepend/<Name>`
entries keep their legacy front-insert precedence ahead of regular
`autoload/<Name>` entries, then dependency ordering may move entries earlier
when another autoload depends on them.

Migration conflict precedence:

- Saved script-owned index entries are the primary runtime source.
- Project-settings autoloads are merged afterward as compatibility entries.
- If a project-settings entry has the same name and canonical path as a
  script-owned entry, the script-owned metadata wins and project settings remain
  a compatibility input.
- If a project-settings entry has the same name but a different path, the
  script-owned entry still wins and the merged index records a hard
  `CONFLICTING_AUTOLOAD_PATH` diagnostic that names both paths.

Final migration target:

1. Runtime startup reads a resolved autoload index.
2. Script-owned entries are primary.
3. Project-settings entries are imported as compatibility entries.
4. Conflicts are hard errors or explicit migration warnings.
5. Project settings autoload UI is retired or converted to a migration tool.

## Implementation Map

| Layer | File(s) | Change |
| --- | --- | --- |
| Index | `modules/foundry_script/gdscript_autoload_index.{h,cpp}` | Add index entry model, build/invalidate API, and lookup queries. |
| Project settings integration | `core/config/project_settings.*`, Foundry Script/editor hooks | Invalidate or rebuild the index when autoload settings change. |
| Analyzer | `modules/foundry_script/gdscript_analyzer.cpp` | Replace direct autoload settings lookups with index queries; allow same-script `class_name`/autoload dual identity. |
| Editor support | `modules/foundry_script/gdscript_editor.cpp`, `editor/script/script_text_editor.cpp`, highlighter | Move completion/navigation/highlighting to the index. |
| LSP | `modules/foundry_script/language_server/` | Use the index for autoload definitions and type metadata. |
| Docgen/test runner | `modules/foundry_script/editor/gdscript_docgen.cpp`, `modules/foundry_script/tests/gdscript_test_runner.cpp` | Preserve behavior while routing metadata through the index where appropriate. |
| Future annotation | `modules/foundry_script/gdscript_parser.*`, `modules/foundry_script/gdscript_analyzer.cpp` | Add `@autoload` built-in annotation and analyzer-backed metadata extraction. |
| Export/runtime | `main/main.cpp`, `editor/export/editor_export_platform.cpp` | Persist/load resolved autoload index metadata and make runtime startup consume it, with project settings as compatibility input. |

## Testing Strategy

V1 tests should prove parity and isolate the new index.

Suggested C++ tests:

- Index builds entries from project settings in project-settings order.
- Index resolves singleton names and paths.
- Index records missing-path and non-`Node` diagnostics.
- Index records same-script autoload/class dual identity.
- Analyzer allows `class_name EventBus` when project autoload `EventBus` points
  to the same script.
- Analyzer still rejects `class_name EventBus` when project autoload `EventBus`
  points elsewhere.
- Value-position autoload references keep resolving to the singleton instance
  type.
- Type-position references to the same-named global class keep resolving to the
  class.
- Completion/highlighting/navigation outputs stay stable for existing autoloads.

Future annotation tests:

- `@autoload` requires root script target.
- `@autoload` requires `class_name`.
- `@autoload` requires a `Node` base.
- `depends_on = [SaveManager]` resolves to another autoload class.
- Missing dependency, non-autoload dependency, duplicate dependency, and cycles
  are hard errors.
- `order_id` accepts constant integer expressions.
- Project-settings and script-owned entries with the same name/path are
  compatible during migration.
- Project-settings and script-owned entries with the same name but different
  paths are conflicts.

Verification commands for implementation PRs:

```bash
python3 -m SCons platform=linuxbsd target=editor dev_build=yes tests=yes module_text_server_fb_enabled=yes -j$(nproc)
./bin/godot.linuxbsd.editor.dev.x86_64 --headless --test --force-colors
```

For fixture behavior changes:

```bash
./bin/godot.linuxbsd.editor.dev.x86_64 --headless --gdscript-generate-tests modules/foundry_script/tests/scripts
```

## Risks

- Index invalidation can become stale if it does not observe all project settings
  and filesystem changes that affect autoload metadata.
- Analyzer integration can accidentally change type/value lookup precedence.
- PackedScene-backed autoload metadata is harder to resolve without loading
  scenes.
- Script-owned autoloads require saved/exported metadata; runtime scanning is not
  acceptable for exported projects.
- Named built-in annotation arguments are broader than `@autoload`; keep that
  scope explicit.
- The transition period can create two sources of truth unless conflicts and
  precedence are clearly defined.

## Milestone Breakdown

### Milestone 1: Project-Settings-Backed Read-Only Index

Build the index from current project settings, add lookup APIs, diagnostics, and
tests. No consumer behavior should change.

### Milestone 2: Analyzer Uses The Index

Move analyzer autoload resolution and same-name class validation to the index.
Preserve current behavior except for allowing an autoload's own script to declare
the same `class_name`.

### Milestone 3: Editor, LSP, Docgen, And Test Runner Consumers

Move remaining direct autoload metadata reads to the index where appropriate.
Keep outputs stable.

### Milestone 4: Dependency And Order Validation Model

Add graph sorting/diagnostics to the index using project-settings order as the
initial order source. This can be tested with synthetic metadata before
`@autoload` exists.

### Milestone 5: `@autoload` Annotation Producer

Add the script annotation, analyzer-backed extraction, dependency resolution, and
script-owned entries while project settings remain compatible.

### Milestone 6: Runtime/Export Migration

Persist resolved script-owned autoload metadata and move runtime startup toward
the index. Project settings become compatibility input.
