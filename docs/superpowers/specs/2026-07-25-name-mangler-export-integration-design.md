# Name Mangler Export Integration Design

## Goal

Issue #799 turns the reusable name-mangling stages from #795–#798 into an opt-in compiled-bytecode
export mode. An export preset can enable name mangling and optionally name a keep-rules file. When
enabled, the Foundry Script export plugin establishes the complete export graph, gathers serialized
binding evidence, applies keep rules, builds one deterministic project-wide rename map, stages that
map only while serializing every script, and then restores the live editor graph.

The existing compiled-bytecode export remains the default script mode. Name mangling defaults off.
With the toggle off, the current per-file compile/serialize path and bytes remain unchanged.

## Approaches Considered

### Sealed export manifest plus pre-serialized script cache (selected)

`EditorExportPlatform` already computes the preset-specific source set after dependency expansion and
include/exclude filtering. A native-only plugin lifecycle hook will expose a sorted, sealed view of
that set after begin/customization hooks have run but before any manifest file or pending generated
file is written.

`EditorExportFoundryScript` uses that one hook to prepare all mangled `.fsb` buffers in memory. It
opens one `FSNameManglerApplication::Transaction`, serializes every script while the graph is staged,
rolls back, and only then publishes the complete path-to-buffer cache. Ordinary per-file callbacks
subsequently emit those buffers.

The transaction therefore never spans unrelated export callbacks, resource customization, event
processing, or output I/O. A generated script/scene/resource that appears after the manifest is
sealed is rejected before it is written.

### Rescan the project in `_export_begin`

This would avoid changing the generic export lifecycle, but `_export_begin` runs before the platform
has resolved selected-resource dependencies, preset include/exclude filters, importer behavior, and
plugin-generated files. It would analyze a different graph from the one shipped and is rejected.

### Defer mangling until `_export_end`

This would observe more late plugin additions, but source files and generated resources may already
have been written. Making it safe would require staging and replaying the entire generic export
output, which is far beyond the script integration boundary and is rejected.

## Preset Contract

`EditorExportPreset` owns two first-class fields beside `script_mode`:

- `script_name_mangling_enabled`, default `false`; and
- `script_name_mangling_keep_rules`, default empty.

They are exposed through bound setters/getters, saved in each `export_presets.cfg` preset section,
loaded with backward-compatible defaults, and copied when a preset is duplicated. The keep-rules
path is retained when the toggle is disabled so a developer can switch the feature off temporarily
without losing configuration.

The Scripts tab adds:

- a **Mangle names** check button; and
- a **Keep-rules file** path field.

Both controls show the persisted state. The toggle is disabled unless the script export mode is
`MODE_SCRIPT_COMPILED_BYTECODE`. The path field is editable only when compiled bytecode and name
mangling are both enabled. Switching script mode does not silently clear either value.

An enabled toggle paired with any non-bytecode script mode is an invalid preset:

- `EditorExportPlatform::can_export()` returns false with a stable configuration error; and
- the Foundry Script export plugin independently refuses to enter mangling orchestration if a
  programmatic caller bypasses `can_export()`.

There is no fallback to text, `.fsc`, or ordinary unmangled bytecode. When the toggle is false, the
keep-rules path is ignored even if populated.

## Native-Only Manifest Lifecycle

The generic export plugin base receives two C++-only hooks. They are not bound through
`FOUNDRY_VIRTUAL`, so scripts, extensions, and existing plugin ABI semantics do not acquire a new
public callback:

1. a preparation hook receives the sorted effective source paths and sorted pending generated-file
   paths immediately before the first output write; and
2. a late-file validation hook receives every generated path added after preparation, before that
   file is saved.

The default implementations accept the manifest and late files without work. Plugins are invoked in
the existing deterministic `get_name()` order. Preparation failure aborts before output. Late-file
failure aborts before the new file is written.

The effective source manifest contains:

- paths selected by the preset's export filter;
- recursively discovered dependencies and autoload dependencies;
- the results of include and exclude filters;
- imported source paths whose runtime resource is reached through `ResourceLoader` remapping; and
- source paths accepted by importer handling.

An importer marked `skip` does not enter the manifest. For an imported source, the source path stays
the stable manifest identity while `ResourceLoader::load(source_path)` supplies the actual imported
resource graph that will ship.

Pending `ExtraFile` entries created by earlier export begin/customization hooks are listed separately
as generated paths. The Foundry integration rejects a generated `.fs`, `.fsc`, `.fsb`, `.tscn`,
`.scn`, `.tres`, or `.res` because its in-memory bytes are not an authoritative on-disk root that
the analysis APIs can compile or load. Other generated file types do not affect script-name safety.

The platform snapshots each plugin's pending-file count before invoking preparation. Only those
exact entries are pre-seal files. An entry added from inside a preparation hook is post-seal and
must pass late-file validation; it cannot escape merely because it shares the first output drain
with the snapshotted entries. Import sidecars are parsed once while sealing and the cached decision
is reused for output, so a plugin cannot observe one imported-source set during preparation and a
different set during emission.

Once preparation succeeds, the manifest is sealed. Any later generated path with one of those
sensitive extensions is rejected through the late-file hook. This covers additions from later
per-file plugins, resource/scene customization completion, and other final extra-file drains. A
late path is never silently emitted outside the analyzed graph.

## Mangled Export Orchestrator

A focused editor-only component,
`modules/foundry_script/editor/fs_name_mangler_export.{h,cpp}`, owns the preparation pipeline. The
export plugin supplies the sealed manifest, debug/release profile, and optional keep-rules path. Its
result contains:

- a sorted map from source script path to prepared output metadata and `.fsb` bytes;
- stable informational keep-log lines; and
- ordered diagnostics on failure.

The result is transactional: output remains empty unless every stage and every script
serialization succeeds.

### Graph discovery and compilation

Manifest paths are sorted before use. Paths whose resource type is `FoundryScript` are compiled or
loaded with `FSCache::get_full_script()` under one export-compilation scope. `.fs` and `.fsc` inputs
produce a remapped `.fsb` output. An existing `.fsb` input is loaded into the same closed graph and
re-serialized at its existing path, preserving its stored `@static_unload` flag. Duplicate canonical
script identities or a script dependency outside the manifest fail closed.

Every remaining manifest path with a loadable `Resource` type is loaded through `ResourceLoader`.
This includes text/binary native resources and imported resources whose source extension differs
from the shipped artifact. Non-resource files are ignored.

Compilation or resource-load failure identifies the stable source path and aborts. The pipeline does
not mark `complete_project_graph=false` to obtain a keep-all fallback; an export that claims to
mangle names must establish a closed graph.

### Evidence, analysis, and staging

Preparation then runs these stages in order:

1. Build `FSNameManglerAnalysis::Input` from every compiled root and set
   `complete_project_graph=true`.
2. Build `FSNameManglerBindingSafety::Input` from every loaded Resource root, collect evidence, and
   apply it. An error or incomplete result aborts with every sorted binding diagnostic.
3. If the preset keep-rules path is nonempty, resolve it as a project path, load it through
   `FSNameManglerKeepRules`, report parser/load diagnostics, and apply its evidence. A missing or
   malformed configured file aborts.
4. Run `FSNameManglerAnalysis::analyze`. Report its sorted keep log as export information.
5. Start one `FSNameManglerApplication::Transaction` using the exact roots and map from analysis.
   Collision, protected-surface, or incomplete-graph diagnostics abort before mutation.
6. Serialize every root in source-path order with a fresh `FSBytecodeExporter`, preserving the
   source's `@static_unload` state.
7. Roll back the application transaction before leaving the synchronous preparation call.

Each serializer owns its normal per-script string table. The shared map, not a shared serializer,
provides project-wide consistency.

### Output cache and ordinary callbacks

The orchestrator builds a local cache and swaps it into `EditorExportFoundryScript` only after the
last serialization and rollback succeed. Per-file callbacks then:

- replace `.fs`/`.fsc` with the cached `.fsb` and request the existing remap behavior;
- replace an input `.fsb` with its cached bytes at the same path without a self-remap; and
- fail if a manifest script path has no prepared entry.

The cache is cleared at `_export_begin`, on every preparation failure, at `_export_end`, and before
the next export. No buffer or rename result from an aborted export can leak into a later export.

When mangling is disabled, the manifest hooks do no work. `_export_file_compiled_bytecode()` remains
the existing implementation, including compile profile, diagnostics, unsupported-global checks,
`@static_unload` handling, output path, and serialization order. This is the byte-for-byte and
path-for-path compatibility boundary.

## Diagnostics

Failures use the existing `EXPORT_MESSAGE_ERROR` veto mechanism and the **Compiled Script Export**
category. Messages identify a stable stage and source/context where applicable:

- invalid preset mode;
- generated sensitive file before manifest sealing;
- late sensitive file after sealing;
- script compile/load or unsupported named global;
- resource load;
- incomplete serialized binding safety;
- keep-rules read/parse/application;
- name analysis;
- rename-map application/collision; and
- per-script serialization or cache mismatch.

Component diagnostics retain their existing `format()` output and are appended in their guaranteed
sorted order. The first summary error and all actionable details are deterministic. Failures never
downgrade to an unmangled export.

Analysis keep decisions are routed as stable informational export messages so scene/rule/RPC/string
retention is diagnosable without printing from the reusable components themselves.

## Testing

Development follows TDD in focused slices:

1. preset defaults, save/load keys, duplication, mode-dependent control state, and invalid-mode
   validation;
2. native manifest hook ordering, sorted effective paths, importer-skip/remap semantics, initial
   generated-sensitive rejection, and late-sensitive rejection before save;
3. sealed-manifest orchestration across multiple `.fs`/`.fsc`/`.fsb` roots and loaded
   scene/resource/import roots;
4. binding-safety, malformed keep-rules, collision, incomplete graph, serialization failure, and
   cache-cleanup diagnostics;
5. toggle-off byte-for-byte/path-for-path comparison against the current compiled-bytecode export;
6. deterministic repeated/reversed discovery producing identical cached buffers;
7. a scratch project exported with the command-first
   `foundry project export --project <dir> --preset <name> --output <pack> --mode pack` flow, followed
   by a headless run using that pack. The project exercises a scene connection, exported property,
   animation binding, keep rule, cross-file dispatch, and a private marker that must be absent from
   the exported `.fsb`.

Generated projects, packs, extracted inspection files, and logs live under `FOUNDRY_TEST_SCRATCH`.
Focused export/name-mangler suites run before the existing analysis, keep-rules, application,
binding-safety, bytecode, resource/scene, and broad Foundry Script suites. Final validation uses a
strict optimized macOS editor build with tests and warnings-as-errors.

## Non-Goals

- rewriting source scenes/resources instead of keeping referenced names;
- changing `.fsb` format version or bytecode layout;
- changing the rename policy or keep-rules syntax from #795/#796;
- expanding script-facing `EditorExportPlugin` callbacks;
- staging/replaying arbitrary generic export output; and
- the larger final leak/parity corpus owned by #800 beyond #799's real-pack acceptance.
