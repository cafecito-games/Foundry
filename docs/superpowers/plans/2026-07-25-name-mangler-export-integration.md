# Name Mangler Export Integration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an opt-in export-preset name-mangling mode that prepares one safe, deterministic
whole-program map and emits runnable mangled `.fsb` files without changing toggle-off exports.

**Architecture:** `EditorExportPlatform` seals a native-only effective-file manifest before output
and asks plugins to prepare against it; later safety-sensitive generated files are rejected.
`FSNameManglerExport` compiles/loads that closed graph, composes #795–#798, serializes every staged
script into a transactional cache, and rolls back before ordinary per-file emission.

**Tech Stack:** C++17, Godot/Foundry editor export APIs, Foundry Script compiler/cache/bytecode APIs,
doctest, SCons, command-first Foundry CLI.

---

## File Structure

- Modify `editor/export/editor_export_preset.h` and
  `editor/export/editor_export_preset.cpp`: first-class preset state and accessors.
- Modify `editor/export/editor_export.cpp`: save/load the new preset keys.
- Modify `editor/export/project_export.h` and `editor/export/project_export.cpp`: Scripts-tab
  controls, mode-dependent enabled state, and preset duplication.
- Modify `editor/export/editor_export_plugin.h` and
  `editor/export/editor_export_plugin.cpp`: native-only manifest preparation and late-file hooks.
- Modify `editor/export/editor_export_platform.cpp`: effective-manifest sealing, importer-skip
  filtering, deterministic hook ordering, invalid-mode validation, and late-file enforcement.
- Create `modules/foundry_script/editor/fs_name_mangler_export.h` and
  `modules/foundry_script/editor/fs_name_mangler_export.cpp`: closed-graph preparation, evidence,
  rules, analysis, transaction, serialization, diagnostics, and output metadata.
- Modify `modules/foundry_script/editor/fs_editor_export_plugin.h` and
  `modules/foundry_script/editor/fs_editor_export_plugin.cpp`: preset guard, manifest integration,
  prepared-cache lifecycle, and per-file emission.
- Create `tests/editor/test_editor_export_manifest.h`: generic manifest lifecycle coverage.
- Create `tests/editor/test_editor_export_name_mangling.h`: preset/schema/validation state coverage.
- Modify `tests/test_main.cpp`: include the two editor test headers.
- Create `modules/foundry_script/tests/test_name_mangler_export.h`: orchestrator, cache, diagnostics,
  compatibility, and command-first pack acceptance.
- Modify `modules/foundry_script/README.md` and `modules/foundry_script/KEEP_RULES.md`: preset usage,
  invalid-mode behavior, and keep-rules path integration.

### Task 1: Preset schema, validation, and Scripts-tab state

**Files:**

- Modify: `editor/export/editor_export_preset.h`
- Modify: `editor/export/editor_export_preset.cpp`
- Modify: `editor/export/editor_export.cpp`
- Modify: `editor/export/project_export.h`
- Modify: `editor/export/project_export.cpp`
- Create: `tests/editor/test_editor_export_name_mangling.h`
- Modify: `tests/test_main.cpp`

- [ ] **Step 1: Write the failing preset contract tests**

Add `tests/editor/test_editor_export_name_mangling.h` with tests named
`[Editor][Export][NameMangler] ...`. Use a minimal `EditorExportPlatform` test double and assert:

```cpp
Ref<TestNameManglerExportPlatform> platform =
		memnew(TestNameManglerExportPlatform);
Ref<EditorExportPreset> preset = platform->create_preset();

CHECK_FALSE(preset->is_script_name_mangling_enabled());
CHECK(preset->get_script_name_mangling_keep_rules().is_empty());
CHECK(preset->is_script_name_mangling_available());

preset->set_script_name_mangling_enabled(true);
preset->set_script_name_mangling_keep_rules(
		"res://mangling.keep");
CHECK(preset->is_script_name_mangling_enabled());
CHECK_EQ(preset->get_script_name_mangling_keep_rules(),
		"res://mangling.keep");

preset->set_script_export_mode(
		EditorExportPreset::MODE_SCRIPT_TEXT);
CHECK_FALSE(preset->is_script_name_mangling_available());
CHECK(preset->is_script_name_mangling_enabled());
```

Add an accessor-backed config round-trip test for exact keys
`script_name_mangling_enabled` and `script_name_mangling_keep_rules`, plus a duplication helper test
that proves both values are copied. Add a platform `can_export()` test expecting false and the exact
configuration text when mangling is enabled outside compiled bytecode.

Include the header from `tests/test_main.cpp` under `TOOLS_ENABLED`.

- [ ] **Step 2: Build and run the RED test**

Run:

```sh
scons platform=macos target=editor dev_mode=yes tests=yes \
  module_text_server_fb_enabled=yes cache_path=/Users/christian/.scons_cache -j8
./bin/foundry.macos.editor.arm64 --headless test run \
  --case "*Export*NameMangler*" --force-colors
```

Expected: compile failure because the preset accessors and UI-state contract do not exist.

- [ ] **Step 3: Add first-class preset state and persistence**

Add these fields and methods to `EditorExportPreset`:

```cpp
bool script_name_mangling_enabled = false;
String script_name_mangling_keep_rules;

void set_script_name_mangling_enabled(bool p_enabled);
bool is_script_name_mangling_enabled() const;
void set_script_name_mangling_keep_rules(const String &p_path);
String get_script_name_mangling_keep_rules() const;
bool is_script_name_mangling_available() const;
```

`is_script_name_mangling_available()` returns exactly:

```cpp
return script_mode == MODE_SCRIPT_COMPILED_BYTECODE;
```

Setters retain values and call `_save_presets_if_available()`. Bind getters and setters in
`_bind_methods()`.

In `EditorExport::_save()` write both exact keys beside `script_export_mode`. In `load_config()`,
load them after `script_export_mode` with defaults `false` and `String()`. Do not normalize away an
invalid persisted combination.

- [ ] **Step 4: Add the blocking core validation**

In `EditorExportPlatform::can_export()`, before platform/plugin checks, reject:

```cpp
if (p_preset->is_script_name_mangling_enabled() &&
		!p_preset->is_script_name_mangling_available()) {
	r_error += TTR(
			"Foundry Script name mangling requires the "
			"Compiled bytecode script export mode.\n");
	valid = false;
}
```

Keep the runtime plugin guard for Task 6 so pack-only/programmatic callers cannot bypass this check.

- [ ] **Step 5: Add and synchronize the Scripts-tab controls**

Add `CheckButton *script_name_mangling` and `LineEdit *script_name_mangling_keep_rules` to
`ProjectExportDialog`, plus:

```cpp
void _script_name_mangling_changed(bool p_enabled);
void _script_name_mangling_keep_rules_changed(
		const String &p_path);
void _update_script_name_mangling_controls();
```

Construct **Mangle names** and **Keep-rules file** controls below `script_mode`. Their handlers write
the current preset unless `updating` is true. `_update_current_preset()` reads both persisted values.
The state helper does exactly:

```cpp
const bool compiled =
		current->is_script_name_mangling_available();
script_name_mangling->set_disabled(!compiled);
script_name_mangling_keep_rules->set_editable(
		compiled &&
		current->is_script_name_mangling_enabled());
```

Call it after preset selection and after `_script_export_mode_changed()`. Extend `_duplicate_preset()`
to copy both fields.

- [ ] **Step 6: Run the focused test and commit**

Run:

```sh
./bin/foundry.macos.editor.arm64 --headless test run \
  --case "*Export*NameMangler*" --force-colors
```

Expected: all preset/schema/validation assertions pass.

Commit:

```sh
git add editor/export/editor_export_preset.h \
  editor/export/editor_export_preset.cpp \
  editor/export/editor_export.cpp \
  editor/export/editor_export_platform.cpp \
  editor/export/project_export.h \
  editor/export/project_export.cpp \
  tests/editor/test_editor_export_name_mangling.h \
  tests/test_main.cpp
git commit -m "Add name mangling export preset controls"
```

### Task 2: Deterministic sealed-manifest lifecycle

**Files:**

- Modify: `editor/export/editor_export_plugin.h`
- Modify: `editor/export/editor_export_plugin.cpp`
- Modify: `editor/export/editor_export_platform.cpp`
- Create: `tests/editor/test_editor_export_manifest.h`
- Modify: `tests/test_main.cpp`

- [ ] **Step 1: Write RED tests for native hook ordering and abort behavior**

Create a native test plugin that records calls through wished-for hooks:

```cpp
struct ExportFileManifest {
	Vector<String> source_paths;
	Vector<String> generated_paths;
};

virtual Error _prepare_export_file_manifest(
		const ExportFileManifest &p_manifest,
		String &r_error) override;
virtual Error _validate_late_export_file(
		const String &p_path,
		String &r_error) const override;
```

Use an export-platform test accessor and capture save callback. Prove:

- source and generated paths arrive sorted;
- include/exclude and dependency-expanded paths are present;
- importer `skip` paths are absent while an imported source path remains the manifest identity;
- all `_begin_customize_resources/_scenes` hooks finish before preparation;
- a preparation error causes zero save callbacks;
- a late validation error occurs before the late file's save callback; and
- plugin call order follows sorted `get_name()`.

- [ ] **Step 2: Build/run RED**

Run:

```sh
scons platform=macos target=editor dev_mode=yes tests=yes \
  module_text_server_fb_enabled=yes cache_path=/Users/christian/.scons_cache -j8
./bin/foundry.macos.editor.arm64 --headless test run \
  --case "*ExportManifest*" --force-colors
```

Expected: compile failure on the missing internal hooks/manifest type.

- [ ] **Step 3: Add the C++-only hook contract**

Add `EditorExportPlugin::ExportFileManifest` and protected virtual methods with default `OK`
implementations. Do not add `FOUNDRY_VIRTUAL` declarations or bindings.

The manifest owns vectors by value so no plugin can mutate the platform's source set. The late hook
is `const` and receives only the path; generated bytes remain owned by the producing plugin.

- [ ] **Step 4: Seal the effective manifest before output**

In `EditorExportPlatform::export_project_files()`:

1. finish preset path/dependency/include/exclude construction;
2. pre-read `.import` configs and remove only importer=`skip` paths from an `effective_paths` set;
3. run every begin-customization hook;
4. collect every currently pending `ExtraFile::path` across plugins;
5. sort source and generated vectors;
6. call `_prepare_export_file_manifest()` in sorted plugin order; and
7. return the hook error after adding a stable `EXPORT_MESSAGE_ERROR`, before
   `add_shared_objects_and_extra_files_from_export_plugins()`.

Use `effective_paths` for both the manifest and main file loop so analysis and output cannot diverge.
Do not resolve imported destinations into new manifest identities: later Resource loading follows
the source path's remap.

Cache the parsed import configs/decisions at sealing and reuse them in the main loop. Snapshot each
plugin's pending `ExtraFile` count before preparation; files appended by preparation itself are
post-seal and therefore go through late validation.

- [ ] **Step 5: Validate every post-seal generated file**

Factor one local helper that calls every plugin's `_validate_late_export_file(path, error)` before a
generated file is saved. Use it in:

- the per-source callback extra-file drain;
- the end-customization/final extra-file drain; and
- any other post-seal `add_shared_objects_and_extra_files_from_export_plugins()` call.

Initial pending extras are already represented in the preparation manifest and are not treated as
late during the first drain.

- [ ] **Step 6: Run focused tests and commit**

Run:

```sh
./bin/foundry.macos.editor.arm64 --headless test run \
  --case "*ExportManifest*" --force-colors
./bin/foundry.macos.editor.arm64 --headless test run \
  --case "*Editor*Export*" --force-colors
```

Expected: manifest tests and existing editor export tests pass.

Commit:

```sh
git add editor/export/editor_export_plugin.h \
  editor/export/editor_export_plugin.cpp \
  editor/export/editor_export_platform.cpp \
  tests/editor/test_editor_export_manifest.h \
  tests/test_main.cpp
git commit -m "Seal export plugin file manifests"
```

### Task 3: Export orchestrator contract and graph discovery

**Files:**

- Create: `modules/foundry_script/editor/fs_name_mangler_export.h`
- Create: `modules/foundry_script/editor/fs_name_mangler_export.cpp`
- Create: `modules/foundry_script/tests/test_name_mangler_export.h`

- [ ] **Step 1: Write the missing-API RED test**

Include the wished-for header and define the public editor-only contract:

```cpp
class FSNameManglerExport {
public:
	struct Input {
		Vector<String> manifest_paths;
		String keep_rules_path;
		bool release_profile = false;
	};

	struct PreparedScript {
		String source_path;
		String output_path;
		Vector<uint8_t> bytes;
		bool remap = false;
	};

	struct Diagnostic {
		String stage;
		String source;
		String message;
		String format() const;
	};

	struct Result {
		Error error = OK;
		RBMap<String, PreparedScript> scripts;
		Vector<String> keep_log;
		Vector<Diagnostic> diagnostics;
	};

	static Result prepare(const Input &p_input);
	static bool is_sensitive_generated_path(
			const String &p_path);
};
```

The first test provides unsorted duplicate/null/missing script roots and expects ordered graph-stage
diagnostics with an empty output map.

- [ ] **Step 2: Build/run RED**

Run:

```sh
scons platform=macos target=editor dev_mode=yes tests=yes \
  module_text_server_fb_enabled=yes cache_path=/Users/christian/.scons_cache -j8
./bin/foundry.macos.editor.arm64 --headless test run \
  --case "*NameManglerExport*" --force-colors
```

Expected: compile failure because `FSNameManglerExport` does not exist.

- [ ] **Step 3: Add transactional result and compile-profile scope**

Implement `Result` publication through a local result that is returned only after success. Add a
private RAII export-compilation scope that:

```cpp
FSLanguage::get_singleton()->set_compiling_for_export(true);
FSCache::begin_script_reload_recording();
```

It disables call-stack tracking only for release, restores both language flags, and recompiles every
recorded source after all application transactions have left scope.

- [ ] **Step 4: Discover and compile/load the closed script graph**

Sort/deduplicate manifest paths. For each path whose resource type is `FoundryScript`, call
`FSCache::get_full_script(path, error, String(), true)` and require valid output. Reject duplicate
`FoundryScript::canonicalize_path()` identities.

Record output metadata exactly:

```cpp
if (extension == "fsb") {
	prepared.output_path = source_path;
	prepared.remap = false;
} else {
	prepared.output_path =
			source_path.get_basename() + ".fsb";
	prepared.remap = true;
}
```

For an input `.fsb`, read its bytes with `FSBytecodeLoader::read_dependencies()` and preserve
`get_annotated_static_unload()`. For `.fs`/`.fsc`, read `annotated_static_unload` from the cached
parser.

Check `FSBytecodeExporter::collect_unsupported_named_globals()` for every root and abort with the
same stable semantics as the existing per-file path.

- [ ] **Step 5: Load resource roots through stable source paths**

For every remaining manifest path with a nonempty Resource type, call
`ResourceLoader::load(path, "", ResourceFormatLoader::CACHE_MODE_IGNORE_DEEP, &error)` and require a
non-null `Resource`. Add it to binding input with the manifest source path. This is what resolves
import-backed resources to their shipped artifacts without changing diagnostic identity.

- [ ] **Step 6: Run graph-discovery tests and commit**

Cover `.fs`, `.fsc`, `.fsb`, imported resource roots, duplicate canonical scripts, missing
dependencies, unsupported named globals, source-order reversal, and empty manifests.

Run:

```sh
./bin/foundry.macos.editor.arm64 --headless test run \
  --case "*NameManglerExport*Graph*" --force-colors
```

Expected: all graph-discovery assertions pass.

Commit:

```sh
git add modules/foundry_script/editor/fs_name_mangler_export.h \
  modules/foundry_script/editor/fs_name_mangler_export.cpp \
  modules/foundry_script/tests/test_name_mangler_export.h
git commit -m "Discover name mangling export graphs"
```

### Task 4: Compose binding evidence, keep rules, and analysis

**Files:**

- Modify: `modules/foundry_script/editor/fs_name_mangler_export.cpp`
- Modify: `modules/foundry_script/tests/test_name_mangler_export.h`

- [ ] **Step 1: Write RED tests for stage composition**

Create a scratch graph with two scripts, a scene connection, stored script property, animation
track, and keep-rules file. Assert wished-for output:

```cpp
CHECK_EQ(result.error, OK);
CHECK(result.scripts.has("res://base.fs"));
CHECK(result.keep_log.has(
		"Keeping \"scene_handler\": "
		"scene/resource reference "
		"(...)"));
CHECK_FALSE(result.keep_log.has(
		"Keeping \"private_helper\""));
```

Add separate tests for:

- malformed and missing configured rules file;
- incomplete binding safety;
- null resource roots; and
- rules path ignored when caller omits it.

Each failure must have an empty script map and sorted diagnostics.

- [ ] **Step 2: Run RED against the graph-only implementation**

Run:

```sh
./bin/foundry.macos.editor.arm64 --headless test run \
  --case "*NameManglerExport*Evidence*" --force-colors
```

Expected: assertions fail because evidence/rules/analysis are not composed.

- [ ] **Step 3: Implement the exact stage sequence**

Build one `FSNameManglerAnalysis::Input`, then:

```cpp
FSNameManglerBindingSafety::Result binding =
		FSNameManglerBindingSafety::collect(
				binding_input, analysis_input);
if (binding.error != OK || !binding.complete ||
		binding.apply_to_input(analysis_input) != OK) {
	// Publish sorted "binding" diagnostics and fail.
}

if (!p_input.keep_rules_path.is_empty()) {
	FSNameManglerKeepRules rules;
	Vector<FSNameManglerKeepRules::Diagnostic>
			rule_diagnostics;
	// load(), then apply_to_input(); any error fails.
}

FSNameManglerAnalysis::Result analysis =
		FSNameManglerAnalysis::analyze(analysis_input);
```

Resolve a relative rules path with `ProjectSettings::globalize_path()` only after normalizing it to a
stable `res://` path. Preserve component diagnostic order, then sort/deduplicate the orchestrator's
wrappers by `(stage, source, message)`.

- [ ] **Step 4: Run evidence and component regression tests**

Run:

```sh
./bin/foundry.macos.editor.arm64 --headless test run \
  --case "*NameManglerExport*Evidence*" --force-colors
./bin/foundry.macos.editor.arm64 --headless test run \
  --case "*NameManglerBindingSafety*" --force-colors
./bin/foundry.macos.editor.arm64 --headless test run \
  --case "*NameManglerKeepRules*" --force-colors
./bin/foundry.macos.editor.arm64 --headless test run \
  --case "*NameManglerAnalysis*" --force-colors
```

Expected: all focused and existing component tests pass.

- [ ] **Step 5: Commit**

```sh
git add modules/foundry_script/editor/fs_name_mangler_export.cpp \
  modules/foundry_script/tests/test_name_mangler_export.h
git commit -m "Analyze sealed export manifests"
```

### Task 5: Transactional serialization and deterministic cache

**Files:**

- Modify: `modules/foundry_script/editor/fs_name_mangler_export.cpp`
- Modify: `modules/foundry_script/tests/test_name_mangler_export.h`

- [ ] **Step 1: Write RED transaction/cache tests**

Assert that a safe private method marker is present in ordinary serialization, absent from prepared
mangled buffers, and the live script serializes byte-identically before/after `prepare()`. Add:

- map collision/protected-surface failure with no bytes;
- forced later-script serialization failure after an earlier script could serialize;
- repeated and reverse-order preparation yielding identical paths/bytes/logs;
- loaded `.fsb` re-export after VM operator-cache execution;
- exact preservation of `.fsb` `@static_unload`; and
- all source scripts valid and unmangled after failure/destructor rollback.

- [ ] **Step 2: Run RED**

Run:

```sh
./bin/foundry.macos.editor.arm64 --headless test run \
  --case "*NameManglerExport*Serialize*" --force-colors
```

Expected: failure because the orchestrator has not applied or serialized the analysis map.

- [ ] **Step 3: Open one narrow application transaction**

After all evidence/analysis succeeds:

```cpp
FSNameManglerApplication::Transaction transaction;
Vector<FSNameManglerApplication::Diagnostic>
		application_diagnostics;
Error error = transaction.begin(
		analysis_input.scripts,
		analysis.rename_map,
		application_diagnostics);
```

Abort on any application diagnostic. While active, perform only ordered
`FSBytecodeExporter::serialize()` calls. Never load resources, call export plugins, add messages,
pump events, or write files in this scope.

- [ ] **Step 4: Serialize locally, roll back, then publish**

Serialize each root with its recorded static-unload flag into a local `RBMap`. Treat an empty buffer
as failure even if the returned `Error` is `OK`. Explicitly call `transaction.rollback()` before
moving the local map into `Result::scripts`; the destructor remains the failure safety net.

Verify every manifest script root has exactly one cache entry and no output-path collision.

- [ ] **Step 5: Run serialization and application suites, then commit**

Run:

```sh
./bin/foundry.macos.editor.arm64 --headless test run \
  --case "*NameManglerExport*Serialize*" --force-colors
./bin/foundry.macos.editor.arm64 --headless test run \
  --case "*NameManglerApplication*" --force-colors
./bin/foundry.macos.editor.arm64 --headless test run \
  --case "*Bytecode*" --force-colors
```

Expected: all tests pass and live-graph byte comparisons match.

Commit:

```sh
git add modules/foundry_script/editor/fs_name_mangler_export.cpp \
  modules/foundry_script/tests/test_name_mangler_export.h
git commit -m "Serialize mangled export graphs transactionally"
```

### Task 6: Wire the preset, manifest, and prepared cache into the plugin

**Files:**

- Modify: `modules/foundry_script/editor/fs_editor_export_plugin.h`
- Modify: `modules/foundry_script/editor/fs_editor_export_plugin.cpp`
- Modify: `modules/foundry_script/tests/test_name_mangler_export.h`

- [ ] **Step 1: Write RED plugin integration and compatibility tests**

Use an accessor subclass to exercise:

- invalid mode + enabled toggle fails the preparation hook before orchestration;
- enabled compiled mode prepares exactly once;
- initial generated sensitive extensions `.fs/.fsc/.fsb/.tscn/.scn/.tres/.res` fail;
- post-seal sensitive extensions fail through the late hook;
- non-sensitive generated files pass;
- `.fs`/`.fsc` callbacks emit cached `.fsb` with remap;
- `.fsb` emits same-path cached bytes without self-remap;
- missing cache entry is an error and skips source; and
- `_export_begin`, failed preparation, `_export_end`, and the next begin leave no cached bytes.

Capture toggle-off `.fs` output using the existing path before the production edit; after wiring,
assert byte-for-byte equality, identical `.fsb` path/remap, and no keep-rules file access when the
toggle is false.

- [ ] **Step 2: Run RED**

Run:

```sh
./bin/foundry.macos.editor.arm64 --headless test run \
  --case "*NameManglerExport*Plugin*" --force-colors
```

Expected: failure because the Foundry plugin does not override manifest hooks or own a cache.

- [ ] **Step 3: Add plugin state and cache cleanup**

Add:

```cpp
bool name_mangling_enabled = false;
bool name_mangling_prepared = false;
RBMap<String, FSNameManglerExport::PreparedScript>
		mangled_scripts;

void _clear_name_mangling_state();
```

Call the clear helper at the start of `_export_begin()`, on every preparation failure, and in
`_export_end()`.

- [ ] **Step 4: Override preparation and late-file validation**

The preparation hook:

1. returns `OK` immediately when toggle false;
2. rejects non-bytecode mode with the exact preset diagnostic;
3. rejects any sensitive pending generated path;
4. invokes `FSNameManglerExport::prepare()` once;
5. routes keep logs as `EXPORT_MESSAGE_INFO`;
6. routes ordered diagnostics as `EXPORT_MESSAGE_ERROR`; and
7. swaps the complete result cache only on success.

The const late hook returns an error for
`FSNameManglerExport::is_sensitive_generated_path(path)` only when name mangling is enabled and the
manifest has been prepared.

- [ ] **Step 5: Emit cached buffers without touching toggle-off code**

At the top of `_export_file_compiled_bytecode()`, dispatch to a new
`_export_file_mangled_bytecode()` only when `name_mangling_enabled`. Leave the body of the original
per-file implementation unchanged.

The mangled method looks up the exact source path, calls `add_file()` with prepared metadata, and
calls `skip()` for same-path `.fsb` replacement. A missing entry calls `skip()` and adds a stable
error.

- [ ] **Step 6: Run focused/plugin/component tests and commit**

Run:

```sh
./bin/foundry.macos.editor.arm64 --headless test run \
  --case "*NameManglerExport*Plugin*" --force-colors
./bin/foundry.macos.editor.arm64 --headless test run \
  --case "*ExportManifest*" --force-colors
./bin/foundry.macos.editor.arm64 --headless test run \
  --case "*NameMangler*" --force-colors
```

Expected: all tests pass, including toggle-off byte/path equality.

Commit:

```sh
git add modules/foundry_script/editor/fs_editor_export_plugin.h \
  modules/foundry_script/editor/fs_editor_export_plugin.cpp \
  modules/foundry_script/tests/test_name_mangler_export.h
git commit -m "Integrate name mangling with bytecode export"
```

### Task 7: Command-first real-pack runtime acceptance

**Files:**

- Modify: `modules/foundry_script/tests/test_name_mangler_export.h`

- [ ] **Step 1: Write the RED scratch-project acceptance test**

Under `FOUNDRY_TEST_SCRATCH`, create a project containing:

- `project.foundry`;
- `export_presets.cfg` with compiled bytecode, mangling enabled, and a keep-rules path;
- base/emitter/receiver/resource scripts;
- a scene connection;
- inherited exported-property override;
- animation track;
- dynamic dispatch protected by the keep file; and
- an unkept private marker.

Spawn:

```sh
foundry --headless project export --project <scratch-project> \
  --preset Mangled --output <scratch>/mangled.pck --mode pack
```

Then spawn the pack headlessly and require a stable success marker. Inspect the PCK/contained `.fsb`
through engine pack APIs and assert:

- `.fs`/`.fsc` source is absent;
- expected `.fsb` entries are present;
- kept scene/rule spellings remain;
- the private marker is absent; and
- runtime output proves connection/export/animation/dynamic/cross-file parity.

- [ ] **Step 2: Run RED against the incomplete integration**

Run:

```sh
./bin/foundry.macos.editor.arm64 --headless test run \
  --case "*NameManglerExport*Pack*" --force-colors
```

Expected: a precise export/runtime/leak assertion fails. Diagnose the failing boundary before any
fix; do not weaken the acceptance assertions.

- [ ] **Step 3: Apply only root-cause integration corrections**

For each failure, follow `superpowers:systematic-debugging`: reproduce, trace whether manifest,
resource evidence, application, cache emission, remap, pack lookup, or runtime dispatch diverges,
then add the smallest test-backed correction in the owning component.

- [ ] **Step 4: Run the pack test twice and commit**

Run the exact pack case twice to prove stale caches/remaps do not influence a second export:

```sh
./bin/foundry.macos.editor.arm64 --headless test run \
  --case "*NameManglerExport*Pack*" --force-colors
./bin/foundry.macos.editor.arm64 --headless test run \
  --case "*NameManglerExport*Pack*" --force-colors
```

Expected: both runs pass with identical inspected `.fsb` bytes.

Commit:

```sh
git add modules/foundry_script/tests/test_name_mangler_export.h \
  modules/foundry_script/editor/fs_name_mangler_export.cpp \
  modules/foundry_script/editor/fs_editor_export_plugin.cpp \
  editor/export/editor_export_platform.cpp
git commit -m "Cover mangled bytecode pack exports"
```

### Task 8: User documentation and final verification

**Files:**

- Modify: `modules/foundry_script/README.md`
- Modify: `modules/foundry_script/KEEP_RULES.md`

- [ ] **Step 1: Document the exact preset contract**

Add concise documentation covering:

```text
Scripts > FoundryScript Export Mode = Compiled bytecode
Scripts > Mangle names = On
Scripts > Keep-rules file = res://path/to/mangling.keep
```

State that a non-bytecode mode plus enabled toggle is invalid, an empty path is allowed, a configured
malformed/missing file aborts export, toggle-off ignores the retained path, and late generated
script/native scene/resource files abort because they are outside the sealed graph.

- [ ] **Step 2: Run format/diff checks and commit docs**

Run:

```sh
git diff --check
python3 misc/scripts/header_guards.py \
  editor/export/editor_export_preset.h \
  editor/export/project_export.h \
  editor/export/editor_export_plugin.h \
  modules/foundry_script/editor/fs_name_mangler_export.h \
  modules/foundry_script/editor/fs_editor_export_plugin.h
```

Expected: no `REQUIRES MANUAL CHANGES` result. If the script converts a legacy guard, include that
mechanical correction with the owning code commit and rerun it.

Commit:

```sh
git add modules/foundry_script/README.md \
  modules/foundry_script/KEEP_RULES.md
git commit -m "Document mangled bytecode exports"
```

- [ ] **Step 3: Run the strict optimized warnings-as-errors build**

Run:

```sh
scons platform=macos target=editor dev_mode=yes tests=yes \
  optimize=speed_trace module_text_server_fb_enabled=yes \
  cache_path=/Users/christian/.scons_cache -j8
```

Expected: exit 0. `optimize=speed_trace` is the repository's debugger-friendly `-O2`; `dev_mode=yes`
enables warnings-as-errors.

- [ ] **Step 4: Run focused tests in dependency order**

Run:

```sh
./bin/foundry.macos.editor.arm64 --headless test run \
  --case "*Export*NameMangler*" --force-colors
./bin/foundry.macos.editor.arm64 --headless test run \
  --case "*ExportManifest*" --force-colors
./bin/foundry.macos.editor.arm64 --headless test run \
  --case "*NameManglerExport*" --force-colors
./bin/foundry.macos.editor.arm64 --headless test run \
  --case "*NameManglerBindingSafety*" --force-colors
./bin/foundry.macos.editor.arm64 --headless test run \
  --case "*NameManglerKeepRules*" --force-colors
./bin/foundry.macos.editor.arm64 --headless test run \
  --case "*NameManglerAnalysis*" --force-colors
./bin/foundry.macos.editor.arm64 --headless test run \
  --case "*NameManglerApplication*" --force-colors
```

Expected: every case and assertion passes.

- [ ] **Step 5: Run broad regression suites**

Run:

```sh
./bin/foundry.macos.editor.arm64 --headless test run \
  --case "*Bytecode*" --force-colors
./bin/foundry.macos.editor.arm64 --headless test run \
  --case "*PackedScene*" --force-colors
./bin/foundry.macos.editor.arm64 --headless test run \
  --case "*Animation*" --force-colors
./bin/foundry.macos.editor.arm64 --headless test run \
  --case "*Resource*" --force-colors
./bin/foundry.macos.editor.arm64 --headless test run \
  --case "*Editor*Export*" --force-colors
./bin/foundry.macos.editor.arm64 --headless test run \
  --case "*FoundryScript*" --force-colors
```

Expected: all doctest summaries report success. Investigate isolated cleanup warnings separately from
case/assertion failures.

- [ ] **Step 6: Verify the exact committed candidate**

Commit any final test-backed corrections, then run:

```sh
git status --short
git diff origin/develop...HEAD --check
git log --oneline origin/develop..HEAD
git rev-parse HEAD
```

Expected: clean worktree, no whitespace errors, focused commits, and an exact candidate SHA ready for
independent spec review. Do not invoke Cursor, push, open a PR, or merge until the orchestrator
releases those gates.
