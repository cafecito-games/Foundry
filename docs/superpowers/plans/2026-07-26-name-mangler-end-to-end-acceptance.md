# Name Mangler End-to-End Acceptance Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use
> superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use
> checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add the final real-export acceptance suite proving Foundry Script name
mangling preserves behavior, strips private names, fails loudly for unescaped
computed dispatch, and is deterministic.

**Architecture:** Extract #799's test-only subprocess/PCK helpers into one
reusable header, then add a separate #800 acceptance header containing a rich
safe parity project and a minimal unsafe-dispatch project. Compare normalized
runtime transcripts and sorted per-script `.fsb` byte maps; do not expose a new
production rename-map API or compare whole PCK containers.

**Tech Stack:** C++17, Godot/Foundry engine test APIs, doctest,
`TemporaryProjectTree`, command-first Foundry CLI, `.fsb` bytecode, SCons,
GitHub CLI, and Cursor Agent.

---

## File Structure

- Create
  `modules/foundry_script/tests/fs_name_mangler_export_test_utils.h`: reusable
  test-only subprocess and scoped PCK-mount primitives.
- Modify `modules/foundry_script/tests/test_name_mangler_export.h`: include the
  helper and remove the identical local definitions; retain #799's test
  behavior.
- Create
  `modules/foundry_script/tests/test_name_mangler_acceptance.h`: generated
  scratch projects, export/run/read helpers, semantic transcript checks,
  leak/keep checks, determinism checks, and unsafe-dispatch checks.
- Modify `modules/foundry_script/fs_function.h`,
  `modules/foundry_script/fs_byte_codegen.{h,cpp}`,
  `modules/foundry_script/fs_bytecode_verifier.{h,cpp}`, and
  `modules/foundry_script/fs_bytecode_loader.cpp`: preserve whether a
  reflection enumeration receiver is the current script instance, and recover
  the same metadata after a bytecode round trip without changing the `.fsb`
  format.
- Modify `modules/foundry_script/fs_name_mangler_analysis.cpp` and
  `modules/foundry_script/fs_name_mangler_application.cpp`: scope resolvable
  reflection retention/defense to the receiver class and included inheritance
  closure while keeping unresolved receivers conservative.
- Modify
  `modules/foundry_script/tests/test_name_mangler_analysis.h`,
  `modules/foundry_script/tests/test_name_mangler_application.h`, and relevant
  bytecode tests: narrow RED/GREEN coverage for issue #1237.
- Keep
  `docs/superpowers/specs/2026-07-26-name-mangler-end-to-end-acceptance-design.md`
  as the normative design.

The first acceptance RED exposed issue #1237: reflection evidence and
application defense were project-wide even when `get_method_list()` had a
known, unrelated receiver class. The production scope above is the diagnosed
fix. Any additional acceptance failure still requires root-cause analysis and
another explicit plan amendment before changing production.

## Task 1: Establish the Baseline and Extract Shared Test Utilities

**Files:**

- Create:
  `modules/foundry_script/tests/fs_name_mangler_export_test_utils.h`
- Modify: `modules/foundry_script/tests/test_name_mangler_export.h`
- Test: `modules/foundry_script/tests/test_name_mangler_export.h`

- [ ] **Step 1: Build the unchanged implementation and run #799's real-pack
  baseline**

Run:

```sh
python3 scripts/agent_build.py --test \
  --case "*NameManglerExport*Pack*" \
  --progress-file /tmp/issue800-baseline-progress.jsonl
```

Expected: strict `dev_mode=yes` build exits 0 and the existing
`Command-first export runs a real mangled pack` case passes. Record its case
and assertion counts. If it fails on unchanged `origin/develop`, investigate
before editing.

- [ ] **Step 2: Add the reusable helper header**

Create
`modules/foundry_script/tests/fs_name_mangler_export_test_utils.h` with the
repository license header and this complete body:

```cpp
#pragma once

#ifdef TOOLS_ENABLED

#include "core/io/file_access.h"
#include "core/io/file_access_pack.h"
#include "core/os/os.h"

#include <cstring>

namespace FSTests {

struct NameManglerPackProcessResult {
	Error error = FAILED;
	int exit_code = -1;
	String output;
};

static NameManglerPackProcessResult name_mangler_export_run_process(
		const List<String> &p_arguments,
		const String &p_working_directory = String()) {
	NameManglerPackProcessResult result;
	Vector<uint8_t> stdout_bytes;
	Vector<uint8_t> stderr_bytes;
	Dictionary pipe_info = OS::get_singleton()->execute_with_pipe(
			OS::get_singleton()->get_executable_path(), p_arguments,
			false, p_working_directory, Dictionary(), false);
	if (pipe_info.is_empty()) {
		return result;
	}

	Ref<FileAccess> stdout_pipe = pipe_info["stdio"];
	Ref<FileAccess> stderr_pipe = pipe_info["stderr"];
	const OS::ProcessID pid = pipe_info["pid"];
	auto pump = [](const Ref<FileAccess> &p_pipe,
						Vector<uint8_t> &r_bytes) {
		if (p_pipe.is_null() || !p_pipe->is_open()) {
			return;
		}
		const uint64_t available = p_pipe->get_length();
		if (available == 0) {
			return;
		}
		Vector<uint8_t> chunk;
		chunk.resize(available);
		const uint64_t read =
				p_pipe->get_buffer(chunk.ptrw(), available);
		const int old_size = r_bytes.size();
		r_bytes.resize(old_size + read);
		if (read > 0) {
			memcpy(r_bytes.ptrw() + old_size, chunk.ptr(), read);
		}
	};

	const uint64_t deadline =
			OS::get_singleton()->get_ticks_msec() + 180000;
	while (OS::get_singleton()->get_ticks_msec() < deadline) {
		pump(stdout_pipe, stdout_bytes);
		pump(stderr_pipe, stderr_bytes);
		if (!OS::get_singleton()->is_process_running(pid)) {
			pump(stdout_pipe, stdout_bytes);
			pump(stderr_pipe, stderr_bytes);
			result.error = OK;
			break;
		}
		OS::get_singleton()->delay_usec(20000);
	}
	if (result.error != OK &&
			OS::get_singleton()->is_process_running(pid)) {
		OS::get_singleton()->kill(pid);
	}
	if (stdout_pipe.is_valid()) {
		stdout_pipe->close();
	}
	if (stderr_pipe.is_valid()) {
		stderr_pipe->close();
	}
	result.exit_code =
			OS::get_singleton()->get_process_exit_code(pid);
	result.output = String::utf8(
			(const char *)stdout_bytes.ptr(), stdout_bytes.size());
	result.output += String::utf8(
			(const char *)stderr_bytes.ptr(), stderr_bytes.size());
	return result;
}

struct NameManglerPackMount {
	bool owns_packed_data = false;

	Error mount(const String &p_path) {
		PackedData *packed_data = PackedData::get_singleton();
		ERR_FAIL_NULL_V(packed_data, ERR_UNAVAILABLE);
		ERR_FAIL_COND_V(!packed_data->get_file_paths().is_empty(),
				ERR_ALREADY_IN_USE);
		owns_packed_data = true;
		return packed_data->add_pack(p_path, false, 0);
	}

	Vector<uint8_t> read(const String &p_path) const {
		PackedData *packed_data = PackedData::get_singleton();
		ERR_FAIL_NULL_V(packed_data, Vector<uint8_t>());
		Ref<FileAccess> file = packed_data->try_open_path(p_path);
		ERR_FAIL_COND_V(file.is_null(), Vector<uint8_t>());
		Vector<uint8_t> bytes;
		bytes.resize(file->get_length());
		if (!bytes.is_empty()) {
			file->get_buffer(bytes.ptrw(), bytes.size());
		}
		return bytes;
	}

	~NameManglerPackMount() {
		if (owns_packed_data &&
				PackedData::get_singleton() != nullptr) {
			PackedData::get_singleton()->clear();
		}
	}
};

} // namespace FSTests

#endif // TOOLS_ENABLED
```

- [ ] **Step 3: Switch the existing export test to the helper**

Add:

```cpp
#include "fs_name_mangler_export_test_utils.h"
```

beside `fs_temporary_project_tree.h` in
`test_name_mangler_export.h`. Delete only the old
`NameManglerPackProcessResult`, `name_mangler_export_run_process`, and
`NameManglerPackMount` definitions. Do not change the existing pack test or its
calls.

- [ ] **Step 4: Verify the extraction is behavior-neutral**

Run:

```sh
python3 scripts/agent_build.py --test \
  --case "*NameManglerExport*Pack*" \
  --progress-file /tmp/issue800-helper-progress.jsonl
git diff --check
```

Expected: build and test exit 0 with the same test behavior as Step 1; diff
check is silent.

- [ ] **Step 5: Commit the extraction**

```sh
git add modules/foundry_script/tests/fs_name_mangler_export_test_utils.h \
  modules/foundry_script/tests/test_name_mangler_export.h
git commit -m "Share name mangler pack test utilities"
```

## Task 2: Scope Reflection Retention to Receiver Classes (#1237)

**Files:**

- Modify: `modules/foundry_script/fs_function.h`
- Modify: `modules/foundry_script/fs_byte_codegen.h`
- Modify: `modules/foundry_script/fs_byte_codegen.cpp`
- Modify: `modules/foundry_script/fs_bytecode_verifier.h`
- Modify: `modules/foundry_script/fs_bytecode_verifier.cpp`
- Modify: `modules/foundry_script/fs_bytecode_loader.cpp`
- Modify: `modules/foundry_script/fs_name_mangler_analysis.cpp`
- Modify: `modules/foundry_script/fs_name_mangler_application.cpp`
- Modify: `modules/foundry_script/tests/test_name_mangler_analysis.h`
- Modify: `modules/foundry_script/tests/test_name_mangler_application.h`
- Modify relevant bytecode test coverage if required by the recovered metadata.

- [ ] **Step 1: Add narrow RED analysis coverage**

Add a two-root regression where one script calls `get_method_list()` on its
known `self` receiver and owns a reflected method, while an independent script
owns a distinct private method. Require:

- the reflected owner's method is kept with `KEEP_REFLECTION`;
- a method inherited by the reflected receiver is also kept;
- the unrelated private method remains in `rename_map`;
- reversing root order gives the same classifications, map, and keep log; and
- an unresolved or non-self reflection receiver remains project-wide and
  conservatively keeps compatible declarations.

Use unique spellings so the project-wide atomic-name policy does not conflate
the declarations. A spelling that occurs both on a reflected receiver and
elsewhere must still be kept globally because the rename map is keyed by
atomic name.

Name the focused case:

```text
[FoundryScript][NameMangler][Reflection][Analysis] Self enumeration scopes retention to the receiver hierarchy
```

- [ ] **Step 2: Add matching RED application-defense coverage**

Build reflected and unrelated roots and require:

- the analysis-produced map, including the unrelated private rename, stages
  and rolls back successfully;
- a hand-authored map for the reflected receiver's visible method is rejected
  atomically as `reflection enumeration`; and
- a hand-authored map for the unrelated private method stages and rolls back
  successfully.

Cover method, property, and signal enumeration where the fixture can remain
narrow. Require inherited visibility for at least the method case and
conservative rejection for an unresolved receiver.

Name the focused case:

```text
[FoundryScript][NameMangler][Reflection][Application] Scoped retention accepts unrelated private maps
```

- [ ] **Step 3: Run the focused tests and preserve the real RED**

Run:

```sh
python3 scripts/agent_build.py --test \
  --case "*NameMangler*Reflection*" \
  --progress-file /tmp/issue1237-reflection-red.jsonl
```

Expected before the production fix: the unrelated same-kind declaration is
kept by analysis and/or rejected by application preflight. Record the first
causal assertion, not later cascade failures.

- [ ] **Step 4: Preserve resolvable receiver metadata without a format bump**

Record per-function reflection-use scope during bytecode generation:

- calls whose native reflection receiver address is `self` record the matching
  method/property/signal enumeration as self-scoped;
- calls whose receiver is another value, the `FSReflection` singleton, or
  otherwise cannot be proven retain an unresolved/conservative flag.

Do not serialize a new field or bump the `.fsb` version. Extend the
authoritative verifier walk to recover the same flags from loaded bytecode
using the verified opcode, receiver address, and method-bind/global-name
tables, and have the loader store that recovered metadata on `FSFunction`.
Add a focused round-trip assertion proving source-compiled and restored
functions expose identical reflection-scope flags.

Name the focused bytecode case:

```text
[FoundryScript][NameMangler][Reflection][Bytecode] Receiver scope survives serialization
```

- [ ] **Step 5: Scope analysis and application to the included class closure**

In analysis, replace kind-only project vectors for resolvable self reflection
with records keyed by the owning `FoundryScript`. Add `KEEP_REFLECTION` only
when the candidate is declared on that class or an included Foundry Script
base visible to it. Keep unresolved reflection sources project-wide.

Mirror the exact rule in application preflight over the original snapshots:
the analysis-produced map must stage, a protected receiver/base declaration
must still be rejected, and an unrelated declaration must no longer be
reported as a protected reflection surface. Do not weaken external,
incomplete-graph, string, RPC, native, scene/resource, annotation, or keep-rule
conservatism.

- [ ] **Step 6: Run RED-to-GREEN verification**

Run:

```sh
python3 scripts/agent_build.py --test \
  --case "*NameMangler*Reflection*" \
  --progress-file /tmp/issue1237-reflection-green.jsonl
./bin/foundry.macos.editor.dev.arm64 --headless test run \
  --case "*NameManglerAnalysis*" --force-colors
./bin/foundry.macos.editor.dev.arm64 --headless test run \
  --case "*NameManglerApplication*" --force-colors
./bin/foundry.macos.editor.dev.arm64 --headless test run \
  --case "*Bytecode*" --force-colors
git diff --check
```

Expected: every focused summary is successful, including restored-bytecode
scope metadata, inherited reflection safety, unrelated private renames, and
conservative unknown receivers.

- [ ] **Step 7: Commit the production fix**

Commit only the #1237 production and focused regression files:

```sh
git add modules/foundry_script/fs_function.h \
  modules/foundry_script/fs_byte_codegen.h \
  modules/foundry_script/fs_byte_codegen.cpp \
  modules/foundry_script/fs_bytecode_verifier.h \
  modules/foundry_script/fs_bytecode_verifier.cpp \
  modules/foundry_script/fs_bytecode_loader.cpp \
  modules/foundry_script/fs_name_mangler_analysis.cpp \
  modules/foundry_script/fs_name_mangler_application.cpp \
  modules/foundry_script/tests/test_name_mangler_analysis.h \
  modules/foundry_script/tests/test_name_mangler_application.h
git commit -m "Scope reflection retention to receiver classes"
```

If the implementation needs another focused bytecode test file, include it in
the same commit and report it explicitly.

## Task 3: Add Safe ON/OFF Parity, Leak, Keep, and Determinism Acceptance

**Files:**

- Create:
  `modules/foundry_script/tests/test_name_mangler_acceptance.h`
- Test:
  `modules/foundry_script/tests/test_name_mangler_acceptance.h`

- [ ] **Step 1: Add exact export, runtime, transcript, and PCK byte-map
  helpers**

Create the new header with the repository license header, `#pragma once`,
`#ifdef TOOLS_ENABLED`, namespace `FSTests`, and these includes:

```cpp
#include "fs_name_mangler_export_test_utils.h"
#include "fs_temporary_project_tree.h"
#include "modules/foundry_script/tests/test_bytecode_serialization.h"

#include "core/io/dir_access.h"
#include "core/templates/rb_map.h"
#include "tests/test_macros.h"
```

Add these helpers:

```cpp
static NameManglerPackProcessResult name_mangler_acceptance_export(
		const String &p_project_root, const String &p_preset,
		const String &p_pack_path) {
	List<String> arguments;
	arguments.push_back("--headless");
	arguments.push_back("project");
	arguments.push_back("export");
	arguments.push_back("--project");
	arguments.push_back(p_project_root);
	arguments.push_back("--preset");
	arguments.push_back(p_preset);
	arguments.push_back("--output");
	arguments.push_back(p_pack_path);
	arguments.push_back("--mode");
	arguments.push_back("pack");
	return name_mangler_export_run_process(arguments);
}

static NameManglerPackProcessResult name_mangler_acceptance_run(
		const String &p_pack_path, const String &p_runtime_root) {
	NameManglerPackProcessResult failed;
	const Error make_error =
			DirAccess::make_dir_recursive_absolute(p_runtime_root);
	if (make_error != OK) {
		failed.error = make_error;
		return failed;
	}
	Ref<DirAccess> filesystem =
			DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	if (filesystem.is_null()) {
		failed.error = ERR_CANT_CREATE;
		return failed;
	}
	const String runtime_pack = p_runtime_root.path_join(
			OS::get_singleton()->get_executable_path().get_file() + ".pck");
	const Error copy_error = filesystem->copy(p_pack_path, runtime_pack);
	if (copy_error != OK) {
		failed.error = copy_error;
		return failed;
	}

	List<String> arguments;
	arguments.push_back("--headless");
	arguments.push_back("project");
	arguments.push_back("run");
	return name_mangler_export_run_process(arguments, p_runtime_root);
}

static String name_mangler_acceptance_transcript(
		const String &p_output) {
	const String prefix = "NAME_MANGLER_ACCEPTANCE|";
	String transcript;
	for (const String &raw_line : p_output.split("\n")) {
		const String line = raw_line.strip_edges();
		if (!line.begins_with(prefix)) {
			continue;
		}
		REQUIRE_MESSAGE(transcript.is_empty(),
				"Runtime emitted more than one acceptance transcript.");
		transcript = line;
	}
	return transcript;
}

static RBMap<String, Vector<uint8_t>>
name_mangler_acceptance_read_scripts(const String &p_pack_path) {
	RBMap<String, Vector<uint8_t>> scripts;
	NameManglerPackMount mount;
	REQUIRE_EQ(mount.mount(p_pack_path), OK);
	PackedData *packed_data = PackedData::get_singleton();
	REQUIRE(packed_data != nullptr);
	for (const String &path : packed_data->get_file_paths()) {
		if (path.get_extension().to_lower() != "fsb") {
			continue;
		}
		const Vector<uint8_t> bytes = mount.read(path);
		REQUIRE_FALSE(bytes.is_empty());
		scripts.insert(path.simplify_path(), bytes);
	}
	return scripts;
}

static bool name_mangler_acceptance_contains(
		const RBMap<String, Vector<uint8_t>> &p_scripts,
		const String &p_marker) {
	for (const KeyValue<String, Vector<uint8_t>> &entry : p_scripts) {
		if (bytecode_buffer_contains(entry.value, p_marker)) {
			return true;
		}
	}
	return false;
}
```

- [ ] **Step 2: Write the safe scratch project**

Add a `name_mangler_acceptance_write_safe_project()` helper that writes these
exact project files through `TemporaryProjectTree::write_file`.

`project.foundry`:

```ini
[application]
config/name="Name Mangler End-to-End Acceptance"
run/main_scene="res://main.tscn"

[rendering]
renderer/rendering_method="gl_compatibility"
```

`export_presets.cfg`:

```ini
[preset.0]
name="Unmangled"
platform="Linux"
runnable=false
dedicated_server=false
custom_features=""
export_filter="all_resources"
include_filter=""
exclude_filter=""
export_path=""
script_export_mode=3
script_name_mangling_enabled=false
script_name_mangling_keep_rules=""

[preset.0.options]
custom_template/debug=""
custom_template/release=""

[preset.1]
name="Mangled"
platform="Linux"
runnable=false
dedicated_server=false
custom_features=""
export_filter="all_resources"
include_filter=""
exclude_filter=""
export_path=""
script_export_mode=3
script_name_mangling_enabled=true
script_name_mangling_keep_rules="res://name-mangler.pro"

[preset.1.options]
custom_template/debug=""
custom_template/release=""
```

`name-mangler.pro`:

```text
-keepclassmembers class ** {
	ruled_dynamic_target;
}
```

`acceptance_trait.fs`:

```foundry
trait_name AcceptanceTrait[T]

abstract func trait_value(value: T) -> T
```

`acceptance_base.fs`:

```foundry
class_name AcceptanceGenericBase[T]
extends Node

signal declared_signal(value: T)
@export var scene_export_value: int = 0
var acceptance_private_member_marker: T

func acceptance_private_method_marker(value: T) -> T:
	acceptance_private_member_marker = value
	declared_signal.emit(value)
	return acceptance_private_member_marker
```

`acceptance_derived.fs`:

```foundry
class_name AcceptanceDerived
extends "res://acceptance_base.fs"[int]
uses AcceptanceTrait[int]

var declared_value: int = 0
var scene_value: int = 0

func declared_handler(value: int) -> void:
	declared_value = value

func scene_handler(value: int) -> void:
	scene_value = value

func trait_value(value: int) -> int:
	return acceptance_private_member_marker + value

@rpc("any_peer", "call_local")
func rpc_surface(value: int) -> int:
	return value + 3

@keep_name
func annotated_dynamic_target(value: int) -> int:
	return value + 4

func ruled_dynamic_target(value: int) -> int:
	return value + 5
```

`emitter.fs`:

```foundry
extends Node

signal scene_surface_signal(value: int)

func fire() -> void:
	scene_surface_signal.emit(7)
```

`reflector.fs`:

```foundry
class_name AcceptanceReflector
extends RefCounted

func reflection_surface_marker() -> int:
	return 1

func reflection_count() -> int:
	var count := 0
	for method in get_method_list():
		if str(method.name).begins_with("reflection_"):
			count += 1
	return count
```

`payload.fs`:

```foundry
class_name AcceptancePayload
extends Resource

@export var resource_export_value: int = 0
```

`payload.tres`:

```ini
[gd_resource type="Resource" load_steps=2 format=3]

[ext_resource type="Script" path="res://payload.fs" id="1_payload"]

[resource]
script = ExtResource("1_payload")
resource_export_value = 11
```

`main.fs`:

```foundry
extends Node

@onready var emitter: Node = $Emitter
@onready var receiver: AcceptanceDerived = $Receiver

func _ready() -> void:
	receiver.declared_signal.connect(receiver.declared_handler)
	var direct_value: int = receiver.acceptance_private_method_marker(40)
	emitter.fire()
	var payload := load("res://payload.tres") as AcceptancePayload
	var widened: AcceptanceTrait[int] = receiver
	var suffixes := PackedStringArray(["dynamic_target"])
	var suffix := suffixes[0]
	var annotated_value: int = receiver.call("annotated_" + suffix, 1)
	var ruled_value: int = receiver.call("ruled_" + suffix, 1)
	var reflection_value := AcceptanceReflector.new().reflection_count()
	var rpc_value := receiver.rpc_surface(39)
	var rpc_config_count: int = int(receiver.get_script().get_rpc_config().size())
	var trait_result := widened.trait_value(2)
	print(vformat(
			"NAME_MANGLER_ACCEPTANCE|declared=%d|scene=%d|scene_export=%d|resource_export=%d|rpc=%d|rpc_config=%d|trait=%d|reflection=%d|annotated=%d|rule=%d|ready=%d",
			receiver.declared_value,
			receiver.scene_value,
			receiver.scene_export_value,
			payload.resource_export_value,
			rpc_value,
			rpc_config_count,
			trait_result,
			reflection_value,
			annotated_value,
			ruled_value,
			1 if direct_value == 40 else 0))
	get_tree().quit(0)
```

`main.tscn`:

```ini
[gd_scene load_steps=4 format=3]

[ext_resource type="Script" path="res://main.fs" id="1_main"]
[ext_resource type="Script" path="res://emitter.fs" id="2_emitter"]
[ext_resource type="Script" path="res://acceptance_derived.fs" id="3_receiver"]

[node name="Main" type="Node"]
script = ExtResource("1_main")

[node name="Emitter" type="Node" parent="."]
script = ExtResource("2_emitter")

[node name="Receiver" type="Node" parent="."]
script = ExtResource("3_receiver")
scene_export_value = 23

[connection signal="scene_surface_signal" from="Emitter" to="Receiver" method="scene_handler"]
```

The reflector file must not preload, inherit, annotate, or type-reference
`AcceptanceGenericBase` or `AcceptanceDerived`.

- [ ] **Step 3: Write the safe acceptance test before changing production**

Add:

```cpp
TEST_CASE("[FoundryScript][NameManglerAcceptance][Parity] Real exports preserve semantics strip names and repeat deterministically") {
	TemporaryProjectTree project(
			"fs_name_mangler_acceptance_safe_" +
			itos(OS::get_singleton()->get_ticks_usec()));
	name_mangler_acceptance_write_safe_project(project);

	const String unmangled_pack =
			project.root.path_join("unmangled.pck");
	const String mangled_a_pack =
			project.root.path_join("mangled_a.pck");
	const String mangled_b_pack =
			project.root.path_join("mangled_b.pck");
	const NameManglerPackProcessResult unmangled_export =
			name_mangler_acceptance_export(
					project.root, "Unmangled", unmangled_pack);
	const NameManglerPackProcessResult mangled_a_export =
			name_mangler_acceptance_export(
					project.root, "Mangled", mangled_a_pack);
	const NameManglerPackProcessResult mangled_b_export =
			name_mangler_acceptance_export(
					project.root, "Mangled", mangled_b_pack);
	INFO("Unmangled export:\n", unmangled_export.output);
	INFO("Mangled export A:\n", mangled_a_export.output);
	INFO("Mangled export B:\n", mangled_b_export.output);
	REQUIRE_EQ(unmangled_export.error, OK);
	REQUIRE_EQ(mangled_a_export.error, OK);
	REQUIRE_EQ(mangled_b_export.error, OK);
	REQUIRE_EQ(unmangled_export.exit_code, 0);
	REQUIRE_EQ(mangled_a_export.exit_code, 0);
	REQUIRE_EQ(mangled_b_export.exit_code, 0);

	const NameManglerPackProcessResult unmangled_runtime =
			name_mangler_acceptance_run(
					unmangled_pack,
					project.root.path_join("runtime_unmangled"));
	const NameManglerPackProcessResult mangled_runtime =
			name_mangler_acceptance_run(
					mangled_a_pack,
					project.root.path_join("runtime_mangled"));
	INFO("Unmangled runtime:\n", unmangled_runtime.output);
	INFO("Mangled runtime:\n", mangled_runtime.output);
	REQUIRE_EQ(unmangled_runtime.error, OK);
	REQUIRE_EQ(mangled_runtime.error, OK);
	REQUIRE_EQ(unmangled_runtime.exit_code, 0);
	REQUIRE_EQ(mangled_runtime.exit_code, 0);
	const String expected =
			"NAME_MANGLER_ACCEPTANCE|declared=40|scene=7|scene_export=23|resource_export=11|rpc=42|rpc_config=1|trait=42|reflection=2|annotated=5|rule=6|ready=1";
	const String unmangled_transcript =
			name_mangler_acceptance_transcript(
					unmangled_runtime.output);
	const String mangled_transcript =
			name_mangler_acceptance_transcript(
					mangled_runtime.output);
	REQUIRE_EQ(unmangled_transcript, expected);
	REQUIRE_EQ(mangled_transcript, expected);
	CHECK_EQ(mangled_transcript, unmangled_transcript);

	const RBMap<String, Vector<uint8_t>> unmangled_scripts =
			name_mangler_acceptance_read_scripts(unmangled_pack);
	const RBMap<String, Vector<uint8_t>> mangled_a_scripts =
			name_mangler_acceptance_read_scripts(mangled_a_pack);
	const RBMap<String, Vector<uint8_t>> mangled_b_scripts =
			name_mangler_acceptance_read_scripts(mangled_b_pack);
	REQUIRE_FALSE(unmangled_scripts.is_empty());
	REQUIRE_EQ(mangled_a_scripts.size(), unmangled_scripts.size());
	REQUIRE_EQ(mangled_b_scripts.size(), mangled_a_scripts.size());
	for (const KeyValue<String, Vector<uint8_t>> &entry :
			mangled_a_scripts) {
		REQUIRE(mangled_b_scripts.has(entry.key));
		CHECK_EQ(mangled_b_scripts[entry.key], entry.value);
	}

	const String private_member =
			"acceptance_private_member_marker";
	const String private_method =
			"acceptance_private_method_marker";
	REQUIRE(name_mangler_acceptance_contains(
			unmangled_scripts, private_member));
	REQUIRE(name_mangler_acceptance_contains(
			unmangled_scripts, private_method));
	CHECK_FALSE(name_mangler_acceptance_contains(
			mangled_a_scripts, private_member));
	CHECK_FALSE(name_mangler_acceptance_contains(
			mangled_a_scripts, private_method));

	const struct {
		const char *path;
		const char *marker;
	} kept[] = {
		{ "res://emitter.fsb", "scene_surface_signal" },
		{ "res://acceptance_derived.fsb", "scene_handler" },
		{ "res://acceptance_base.fsb", "scene_export_value" },
		{ "res://payload.fsb", "resource_export_value" },
		{ "res://acceptance_derived.fsb", "rpc_surface" },
		{ "res://main.fsb", "_ready" },
		{ "res://reflector.fsb", "reflection_surface_marker" },
		{ "res://acceptance_derived.fsb",
				"annotated_dynamic_target" },
		{ "res://acceptance_derived.fsb",
				"ruled_dynamic_target" },
	};
	for (const auto &expectation : kept) {
		CAPTURE(expectation.path);
		CAPTURE(expectation.marker);
		REQUIRE(mangled_a_scripts.has(expectation.path));
		CHECK(bytecode_buffer_contains(
				mangled_a_scripts[expectation.path],
				expectation.marker));
	}
}
```

- [ ] **Step 4: Build and capture the honest first acceptance result**

Run:

```sh
python3 scripts/agent_build.py --test \
  --case "*NameManglerAcceptance*Parity*" \
  --progress-file /tmp/issue800-parity-red-progress.jsonl
```

Expected: the new test is discovered. Record the first precise failure as RED
if any export, runtime, transcript, keep, leak, or determinism boundary is
unmet. Do not weaken the fixture or assertions. If all assertions pass on the
first run, record that the issue was a missing acceptance gate and that no
production behavior change was needed.

- [ ] **Step 5: Resolve only a real, root-caused failure**

If Step 4 is RED:

1. read the complete export/runtime diagnostic;
2. reproduce the same failing boundary;
3. compare with the corresponding focused component test;
4. state one root-cause hypothesis;
5. add the narrowest focused regression assertion; and
6. amend this plan with the exact production file before changing it.

If Step 4 is green, skip this step. Never change production code merely to
make the acceptance fixture easier.

- [ ] **Step 6: Re-run parity twice and commit**

Run:

```sh
python3 scripts/agent_build.py --test \
  --case "*NameManglerAcceptance*Parity*" \
  --progress-file /tmp/issue800-parity-green-a.jsonl
./bin/foundry.macos.editor.dev.arm64 --headless test run \
  --case "*NameManglerAcceptance*Parity*" \
  --progress-format=jsonl \
  --progress-file /tmp/issue800-parity-green-b.jsonl \
  --force-colors
git diff --check
```

Expected: both runs pass with the exact canonical transcript and identical
per-script maps.

Commit:

```sh
git add modules/foundry_script/tests/test_name_mangler_acceptance.h
git commit -m "Prove name mangler export parity"
```

## Task 4: Add Explicit Unsafe Computed-Dispatch Failure Acceptance

**Files:**

- Modify:
  `modules/foundry_script/tests/test_name_mangler_acceptance.h`
- Test:
  `modules/foundry_script/tests/test_name_mangler_acceptance.h`

- [ ] **Step 1: Add the unsafe project writer**

Add `name_mangler_acceptance_write_unsafe_project()` with these exact files.

`project.foundry`:

```ini
[application]
config/name="Name Mangler Unsafe Dispatch Acceptance"
run/main_scene="res://main.tscn"

[rendering]
renderer/rendering_method="gl_compatibility"
```

`export_presets.cfg`:

```ini
[preset.0]
name="Mangled"
platform="Linux"
runnable=false
dedicated_server=false
custom_features=""
export_filter="all_resources"
include_filter=""
exclude_filter=""
export_path=""
script_export_mode=3
script_name_mangling_enabled=true
script_name_mangling_keep_rules=""

[preset.0.options]
custom_template/debug=""
custom_template/release=""
```

`main.fs`:

```foundry
extends Node

func unsafe_dynamic_target() -> void:
	print("UNSAFE_DYNAMIC_TARGET_EXECUTED")

func _exit_after_probe() -> void:
	get_tree().quit(0)

func _ready() -> void:
	get_tree().process_frame.connect(_exit_after_probe, CONNECT_ONE_SHOT)
	var parts := PackedStringArray(["unsafe_dynamic_", "target"])
	var target := parts[0] + parts[1]
	call(target)
```

`main.tscn`:

```ini
[gd_scene load_steps=2 format=3]

[ext_resource type="Script" path="res://main.fs" id="1_main"]

[node name="Main" type="Node"]
script = ExtResource("1_main")
```

The complete spelling `unsafe_dynamic_target` must not appear anywhere else in
the generated project.

- [ ] **Step 2: Write the unsafe acceptance test**

Add:

```cpp
TEST_CASE("[FoundryScript][NameManglerAcceptance][DynamicDispatch] Unescaped computed calls fail with the reconstructed target") {
	TemporaryProjectTree project(
			"fs_name_mangler_acceptance_unsafe_" +
			itos(OS::get_singleton()->get_ticks_usec()));
	name_mangler_acceptance_write_unsafe_project(project);
	const String pack_path = project.root.path_join("unsafe.pck");
	const NameManglerPackProcessResult export_result =
			name_mangler_acceptance_export(
					project.root, "Mangled", pack_path);
	INFO("Unsafe export:\n", export_result.output);
	REQUIRE_EQ(export_result.error, OK);
	REQUIRE_EQ(export_result.exit_code, 0);

	const NameManglerPackProcessResult runtime_result =
			name_mangler_acceptance_run(
					pack_path,
					project.root.path_join("runtime"));
	INFO("Unsafe runtime:\n", runtime_result.output);
	REQUIRE_EQ(runtime_result.error, OK);
	const String lower_output = runtime_result.output.to_lower();
	CHECK(lower_output.contains("unsafe_dynamic_target"));
	CHECK(lower_output.contains("nonexistent function") ||
			lower_output.contains("invalid call") ||
			lower_output.contains("method not found"));
	CHECK_FALSE(runtime_result.output.contains(
			"UNSAFE_DYNAMIC_TARGET_EXECUTED"));

	const RBMap<String, Vector<uint8_t>> scripts =
			name_mangler_acceptance_read_scripts(pack_path);
	REQUIRE(scripts.has("res://main.fsb"));
	CHECK_FALSE(bytecode_buffer_contains(
			scripts["res://main.fsb"],
			"unsafe_dynamic_target"));
}
```

The diagnostic phrase assertion is intentionally narrow to explicit
missing-call forms. Do not replace it with an exit-code-only assertion.

- [ ] **Step 3: Run and capture RED or confirmed behavior**

Run:

```sh
python3 scripts/agent_build.py --test \
  --case "*NameManglerAcceptance*DynamicDispatch*" \
  --progress-file /tmp/issue800-dynamic-red-progress.jsonl
```

Expected: export succeeds; runtime output explicitly names
`unsafe_dynamic_target` as missing/invalid; the target body sentinel is absent;
the emitted bytecode lacks the complete target spelling. If any boundary
fails, use systematic debugging and do not broaden the accepted diagnostics.

- [ ] **Step 4: Run the complete acceptance header twice and commit**

Run:

```sh
python3 scripts/agent_build.py --test \
  --case "*NameManglerAcceptance*" \
  --progress-file /tmp/issue800-acceptance-green-a.jsonl
./bin/foundry.macos.editor.dev.arm64 --headless test run \
  --case "*NameManglerAcceptance*" \
  --progress-format=jsonl \
  --progress-file /tmp/issue800-acceptance-green-b.jsonl \
  --force-colors
git diff --check
```

Expected: both acceptance cases pass twice.

Commit:

```sh
git add modules/foundry_script/tests/test_name_mangler_acceptance.h
git commit -m "Cover unsafe name mangler dispatch"
```

## Task 5: Independent Acceptance-Matrix Audit

**Files:**

- Review only:
  `docs/superpowers/specs/2026-07-26-name-mangler-end-to-end-acceptance-design.md`
- Review only:
  `modules/foundry_script/tests/test_name_mangler_acceptance.h`
- Review only: branch diff against `origin/develop`

- [ ] **Step 1: Start one bounded high-reasoning read-only reviewer**

Dispatch a separate `gpt-5.6-sol` high/xhigh agent after Tasks 3 and 4 are
implemented. Give it only this bounded job:

```text
Read issue #800, epic #786, the approved design, the merged name-mangler
implementation, and the branch diff. Audit the acceptance matrix
line-by-line: ON/OFF semantic parity; declared and scene-connected signals;
scene and Resource exports; RPC config/behavior; engine virtual;
cross-file generic inheritance; generic trait dispatch; reflection scope;
@keep_name and keep-rule dispatch; explicit unescaped dispatch failure;
all-.fsb private member/method absence; intentional kept-name presence; and
repeat-export per-script determinism. Remain read-only. Report COMPLIANT or
concrete gaps with file/line evidence. Separate in-scope fixes from genuine
follow-ups.
```

Report the reviewer start to the epic orchestrator, including the model and
bounded read-only scope.

- [ ] **Step 2: Triage every finding**

For each finding:

- validate it against actual code and test output;
- fix in-scope correctness or coverage gaps using a new RED assertion first;
- file any genuine out-of-scope work as a native GitHub sub-issue of #786,
  add it to Experiment, and tell the epic orchestrator; and
- rerun the focused acceptance suite.

No chat-only deferrals are allowed.

- [ ] **Step 3: Obtain a compliant audit result and commit any fixes**

Expected: reviewer reports `COMPLIANT`, or a follow-up issue exists and the
epic orchestrator explicitly schedules it before epic closure. Commit each
test-backed in-scope correction with an imperative subject.

## Task 6: Strict and Full Verification

**Files:**

- Verify all branch changes

- [ ] **Step 1: Run focused acceptance and existing export smoke**

```sh
python3 scripts/agent_build.py --test \
  --case "*NameManglerAcceptance*" \
  --progress-file /tmp/issue800-focused-acceptance.jsonl
./bin/foundry.macos.editor.dev.arm64 --headless test run \
  --case "*NameManglerExport*Pack*" --force-colors
```

Expected: all cases and assertions pass.

- [ ] **Step 2: Run the full name-mangler matrix**

```sh
./bin/foundry.macos.editor.dev.arm64 --headless test run \
  --case "*NameMangler*" \
  --progress-format=jsonl \
  --progress-file /tmp/issue800-name-mangler.jsonl \
  --force-colors
```

Expected: zero failed cases and assertions.

- [ ] **Step 3: Run bytecode and editor-export regression slices**

```sh
./bin/foundry.macos.editor.dev.arm64 --headless test run \
  --case "*Bytecode*" --force-colors
./bin/foundry.macos.editor.dev.arm64 --headless test run \
  --case "*Editor*Export*" --force-colors
```

Expected: both summaries report success.

- [ ] **Step 4: Run a fresh strict optimized warnings-as-errors build**

```sh
python3 scripts/agent_build.py \
  --progress-file /tmp/issue800-strict-build.jsonl \
  --scons-arg=optimize=speed_trace
```

Expected: `dev_mode=yes dev_build=yes tests=yes` build exits 0. The wrapper
auto-selects macOS and uses the shared SCons cache from `custom.py`.

- [ ] **Step 5: Run the exact full suite with structured progress**

```sh
FOUNDRY_TEST_SCRATCH="$PWD/.test_scratch" \
  ./bin/foundry.macos.editor.dev.arm64 --headless test run \
  --progress-format=jsonl \
  --progress-file /tmp/issue800-full-suite.jsonl \
  --progress-heartbeat-seconds 30 \
  --force-colors
```

Expected: the JSONL `run_end` reports zero failed tests and the doctest summary
reports `Status: SUCCESS!`. Inspect the structured file rather than scraping
mixed console output for progress.

- [ ] **Step 6: Verify exact candidate hygiene**

```sh
git diff --check origin/develop...HEAD
git status --short --untracked-files=all
git log --oneline origin/develop..HEAD
```

Expected: only intentional committed design, plan, and test files differ;
status is clean; no generated pack, runtime, `.foundry`, `.uid`, or scratch
artifact is tracked.

## Task 7: Cursor Convergence, PR, CI, Merge, and Cleanup

**Files:**

- Review branch diff against freshly fetched `origin/develop`

- [ ] **Step 1: Run a fresh read-only Cursor review**

Commit every intended change, fetch `origin/develop`, then run the exact
foreground command from the `cursor-review` skill with:

```sh
CURSOR_REVIEW_BASE=origin/develop
CURSOR_REVIEW_WORKSPACE="$PWD"
```

Expected: valid structured output. Triage every finding with
`superpowers:receiving-code-review`; use systematic debugging and RED tests for
real bugs.

- [ ] **Step 2: Repeat until Cursor reports clean**

After every in-scope fix: run focused and broad verification, commit, and run a
fresh review on the new HEAD. Do not proceed until the latest valid output says
exactly:

```text
RESULT: clean
FINDINGS:
- none
```

- [ ] **Step 3: Push and open the PR**

```sh
git push -u origin issue-800
gh pr create --repo cafecito-games/Foundry --base develop \
  --head issue-800 \
  --title "Prove name mangler behavior end to end" \
  --body-file /tmp/issue800-pr-body.md
```

The PR body must summarize the semantic parity corpus, unsafe dispatch proof,
leak/keep assertions, repeat-export determinism, exact test evidence, Cursor
rounds, independent audit, and the scoped-reflection #1237 fix. End it with:

```text
Closes #1237
Closes #800
```

- [ ] **Step 4: Enable squash auto-merge and monitor all required CI**

```sh
gh pr merge --repo cafecito-games/Foundry --squash --auto
gh pr checks --repo cafecito-games/Foundry --watch --interval 30
```

Expected: all required checks pass and GitHub merges the PR. If CI fails, use
the `github:gh-fix-ci` and systematic-debugging workflows; push a tested fix,
rerun Cursor on the new HEAD, and continue monitoring.

- [ ] **Step 5: Verify closure and clean the branch**

After merge:

```sh
gh issue view 800 --repo cafecito-games/Foundry \
  --json state,projectItems,url
gh issue view 1237 --repo cafecito-games/Foundry \
  --json state,projectItems,url
git -C /Users/christian/CafecitoGames/Foundry fetch origin develop
git -C /Users/christian/CafecitoGames/Foundry worktree remove \
  /Users/christian/CafecitoGames/Foundry/.worktrees/issue-800
git -C /Users/christian/CafecitoGames/Foundry branch -D issue-800
if git -C /Users/christian/CafecitoGames/Foundry ls-remote \
  --exit-code --heads origin issue-800 >/dev/null; then
  git -C /Users/christian/CafecitoGames/Foundry push origin \
    --delete issue-800
fi
```

Expected: #800 and #1237 are closed, both Experiment statuses are Done, the PR
is merged, and the worktree plus local/remote branch are absent. Report the
merge SHA, Cursor rounds, independent audit result, verification counts,
follow-ups (or none), and cleanup result to the epic orchestrator.
