# Compiled Script Export (`.fsb`) Design

## Problem

Exported games ship Foundry Script as `.fsc` binary token streams (`EditorExportFoundryScript` in `modules/foundry_script/register_types.cpp`, default `MODE_SCRIPT_BINARY_TOKENS_COMPRESSED`). The token stream preserves every identifier, string literal, and constant (identifiers are only XOR-masked with `0xb6`), so anyone with this fork's source can reconstruct near-perfect readable source from an exported game. Only comments and exact whitespace are lost.

## Goal

A new export script mode that ships **serialized compiled bytecode** (`.fsb`). At export time the editor runs the full toolchain (parse → analyze → compile) and serializes the compiled `FoundryScript` object graph. At runtime the exported game deserializes and **links** (re-resolves process-bound pointers) without ever running the tokenizer, parser, analyzer, or compiler. Original source structure, local/parameter names, and comments become unrecoverable.

### Accepted residual information

The VM dispatches by `StringName` at runtime, so any faithful compiled form retains:

- Member/method/signal names used by name-dispatched opcodes (`global_names`).
- `MethodInfo` names and argument names (reflection, `Callable`, RPC, virtual dispatch).
- String/NodePath/signal-name constants.
- Engine API names inside pointer-fixup keys.

Stripping these is a **separate follow-up epic**: an export-time whole-program name mangler (auto-analysis plus a keep-rules file for names built dynamically at runtime). This spec covers the bytecode format only.

### Decisions locked in with the user

- Bytecode format first; name mangler is a later epic layered on top.
- `.fsc` and text export modes remain available as preset options.
- Scenes containing built-in (embedded) scripts **fail the export** with an error listing every offending scene — embedded scripts ship as plaintext inside `.tscn`/`.scn` and would silently leak source otherwise.

## Architecture

### New files

- `modules/foundry_script/fs_bytecode_format.h` — shared constants: magic `FSBC`, `FORMAT_VERSION`, engine guard (`VERSION_FULL_CONFIG` string, `sizeof(real_t)`, opcode-table fingerprint: `OPCODE_END` value + opcode count), section IDs.
- `modules/foundry_script/fs_bytecode_export.{h,cpp}` — writer (`#ifdef TOOLS_ENABLED`).
- `modules/foundry_script/fs_bytecode_loader.{h,cpp}` — reader + linker + verifier (all builds).

### Container layout

1. Header: magic, format version, engine guard (hard error on mismatch).
2. Deduped `StringName` table.
3. External-resource table (paths + types, like binary `.scn`).
4. Skeleton section: class tree (fully qualified name, local/global name, base as path+fqcn, native class, subclass tree) readable without bodies — the `.fsb` analog of `FSCompiler::make_scripts`, which is what makes the existing shallow/full cycle handling work.
5. Per-class body sections.
6. Trait conformance + runtime witness section.
7. Dependency list (so `get_dependencies` never parses).

### Serialization surface

Everything `FSCompiler::_prepare_compilation` / `_compile_class` populate on `FoundryScript` (`foundry_script.h`): `member_indices` (full `MemberInfo` including `data_type`, `property_info`, `type_argument_binding`), `_signals`, `constants`, enums, merged `rpc_config`, `member_functions` plus `implicit_initializer`/`implicit_ready`/`static_initializer` and `static_variables_indices`, `subclasses`, traits/`script_trait_list`/`abstract_trait_requirements`, `type_parameters` and generics bindings, all annotation-usage maps, `tool`/`abstract`/`vararg` flags, `local_name`/`global_name`/`fully_qualified_name`.

Per `FSFunction` (`fs_function.h`): `code`, `constants` (tagged encoder, below), `global_names`, `builtin_method_names`, `method_info`, `argument_types`/`return_type`, default-argument info, `_stack_size`/`_instruction_args_size`/`temporary_slots` (the release VM reads `temporary_slots` on every call), `_initial_line`, `name`, RPC info, and recursive `lambdas` with `{capture_count, use_self}`. All `DEBUG_ENABLED`-only fields are excluded — the format is the release subset.

`FSDataType` serializes script types as **(path, fqcn)**, never pointers or refs (fork rule: persist data-type descriptors, not script refs, to avoid the known local-class strong-ref leak). Re-resolved at link via `FSCache` + `find_class(fqcn)`.

### The three hard problems (compiler-side changes, not just I/O)

1. **Validated-pointer tables are not reconstructible from a finished `FSFunction`.** The release layout stores raw function pointers (`operator_funcs`, `setters`/`getters`, `keyed_*`, `indexed_*`, `builtin_methods`, `constructors`, `utilities`, `gds_utilities`); descriptors exist only as DEBUG display strings. `FSByteCodeGenerator` is extended to record structured descriptors in parallel vectors under `TOOLS_ENABLED`: operator → op + both operand types; setter/getter/keyed/indexed → `Variant::Type` + name; builtin method → type + name; constructor → type + argument signature; utility → name; `MethodBind*` → class + method (via `MethodBind::get_instance_class()`). The loader re-resolves through `Variant::get_validated_*` / `ClassDB`; **any resolution failure aborts the load with a clear error**, doubling as the engine-drift guard.

2. **Constant pools contain process-bound objects.** `preload` compiles to a raw `Ref<Resource>` in constants. `encode_variant(full_objects=true)` must never be used — it property-dumps the object, and a preloaded script's storage properties include `script/source`, which would embed plaintext source into the `.fsb`. Instead: tagged Variant encoding where `Ref<Resource>` constants become external-table indices (relinked via `ResourceLoader::load` at link time, which follows `.remap`/`.import` indirection); script refs, `Ref<FSNativeClass>`, singleton pointers, and container-type descriptor Dictionaries embedding script objects each get a dedicated tag; `RID`/`Callable`/`Signal`/ObjectID constants are rejected at export with a loud error; any unknown Object type fails the export.

3. **`OPCODE_STORE_GLOBAL` bakes a process-specific global-array index inline in `code`** (emitted for autoloads) — editor and template ClassDB layouts differ. The codegen records `{code_offset, global_name}` pairs under `TOOLS_ENABLED`; the loader rewrites the inline operands by name. Export also validates that the TOOLS-only `OPCODE_STORE_NAMED_GLOBAL` never reaches an `.fsb`.

### Load path

- `ResourceFormatLoaderFoundryScript`: add `"fsb"` to `get_recognized_extensions`/`get_resource_type`; `get_dependencies` branches on extension and reads the header dependency list instead of UTF-8-parsing the file.
- `FSCache::get_shallow_script`: new `.fsb` branch that instantiates the shell from the skeleton section. `FSCache::get_full_script`: new branch that runs the linker instead of `reload()`, inside the same `WorkerThreadPool::thread_enter_unlock_allowance_zone` (the linker calls `ResourceLoader::load` for externals — required to avoid threaded-load deadlocks on web/mobile). The existing publish-before-link ordering and `set_path_cache(String())`/`set_path()` sequence is preserved so cyclic script references keep working. `FSParserRef::raise_status` guards against `.fsb`-backed scripts.
- Link completion mirrors `reload()`'s tail: populate members, re-register trait conformances and runtime witnesses with `FSConformanceRegistry` (the registry is normally filled by the analyzer/compiler; skipping them silently breaks `extend X uses Trait` runtime checks unless re-registered), `_static_default_init()`, `valid = true`, `_static_init()`, `FSCache::add_static_script` as flagged.
- **Verification pass at link time**: the release VM has zero bytecode bounds checking, so a corrupt or hostile `.fsb` is memory corruption. The loader walks the opcode stream, decodes the 24-bit packed addresses, and validates every index against `_stack_size`/constant count/global-name count/member count before accepting a function.
- **`reload()` guard**: `.fsb`-backed scripts have empty `source`/`binary_tokens`; the non-tools-gated `FSLanguage::reload_scripts` would otherwise invalidate them. Bytecode-backed scripts are marked; `reload()` re-links from the `.fsb` (or no-ops) instead of parsing.

### Export path

- Append `MODE_SCRIPT_COMPILED_BYTECODE` to `EditorExportPreset::ScriptExportMode` (`editor/export/editor_export_preset.h`, append-only to keep saved presets valid) plus the option item in `editor/export/project_export.cpp`.
- `EditorExportFoundryScript::_export_file`: new branch that runs `FSCache::get_full_script` (full editor toolchain, in-process, where autoloads and ProjectSettings exist), serializes, and calls `add_file(basename + ".fsb", data, true)` — inheriting the existing `.remap` machinery so `ext_resource path="res://x.fs"` keeps resolving. Compile/serialize errors fail the export via `add_message(EXPORT_MESSAGE_ERROR)` with the script path and message.
- **Export-time compile profile**: editor builds force `track_call_stack` on, which emits `OPCODE_LINE` everywhere. The `.fsb` compile uses the target profile instead: tracking off for release exports (performance, and line structure is part of what irreversibility hides), on for debug exports. Breakpoint/assert emission follows the same profile.
- **Built-in script check**: when the mode is `MODE_SCRIPT_COMPILED_BYTECODE`, scenes containing embedded FoundryScript sub-resources fail the export with an error listing every offending scene.

## Testing

- Unit codec tests (string table, tagged Variant encoder, version guard) as doctest suites following `tests/test_<area>.h` conventions.
- Single-function round-trip: compile → serialize → deserialize → link → execute; compare against the directly-compiled result.
- Whole-corpus mode: parameterize the existing fixture corpus (`modules/foundry_script/tests/scripts/`) through compile → serialize → load → run, diffing the same `.out` files — mirroring the existing `--use-binary-tokens` mechanism in the test runner.
- Cache/loader integration: cycles, `get_dependencies`, `reload()` guard, conformance re-registration, threaded loads.
- Hardening: corrupt-input tests against the link-time verifier; an assertion that a produced `.fsb` contains no source text or local/parameter identifier bytes; a format-version pin test (pattern: the `TOKENIZER_VERSION` pin).
- End-to-end: export a test project; confirm the PCK contains only `.fsb` (no `.fs`/`.fsc`), the game runs with signals/exports/RPC/static variables/lambdas/traits intact, and a scene with a built-in script fails the export.

## Out of scope / follow-ups

- Whole-program name mangler (auto-analysis + keep-rules file) — follow-up epic.
- Stripping parser/analyzer from `.fsb`-only export templates for binary size.
- No grammar change: `GRAMMAR.md` is unaffected.
