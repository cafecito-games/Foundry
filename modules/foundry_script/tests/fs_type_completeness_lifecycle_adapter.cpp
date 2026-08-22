/**************************************************************************/
/*  fs_type_completeness_lifecycle_adapter.cpp                            */
/**************************************************************************/
/*                         This file is part of:                          */
/*                              GODOT ENGINE                              */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "fs_type_completeness_lifecycle_adapter.h"

#include "fs_test_language_lifecycle.h"
#include "fs_type_completeness_case_id.h"
#include "fs_type_completeness_destination_wrapper_adapter.h"

#include "../fs_analyzer.h"
#include "../fs_bytecode_export.h"
#include "../fs_bytecode_loader.h"
#include "../fs_cache.h"
#include "../fs_compiler.h"
#include "../fs_parser.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/object/ref_counted.h"
#include "core/os/mutex.h"

namespace FSTests {

namespace {

static const String lifecycle_adapter_id = "lifecycle";
static thread_local bool corrupt_transition_artifact_for_test = false;
static thread_local bool load_transition_artifact_from_source_for_test = false;
static thread_local bool skip_transition_invalidation_for_test = false;

// One member type the transition has to carry, and a value it can be initialized with. The spelling
// is the declaration the program contains; the observation renders what came back out of the
// transition and compares it against what went in, so nothing here is an expected outcome.
struct LifecycleDestinationShape {
	const char *leaf;
	const char *spelling;
	const char *initializer;
};

static const LifecycleDestinationShape lifecycle_destination_shapes[] = {
	{ "plain", "uint", "0U" },
	{ "union", "uint | String", "0U" },
	{ "optional", "uint?", "null" },
	{ "container_element", "Array[uint]", "[0U]" },
	{ "tuple_field", "(uint, String)", "(0U, \"a\")" },
};

// What the source declares after the stale stage revises it. It only has to be a type no rendered
// destination spells, so an artifact taken before the revision is distinguishable from the source
// that exists when it is loaded. The stage checks that rather than assuming it.
static const char *lifecycle_revised_spelling = "String";
static const char *lifecycle_revised_initializer = "\"revised\"";

static const LifecycleDestinationShape *find_destination_shape(const String &p_leaf) {
	for (const LifecycleDestinationShape &shape : lifecycle_destination_shapes) {
		if (p_leaf == shape.leaf) {
			return &shape;
		}
	}
	return nullptr;
}

static bool read_coordinate(const Dictionary &p_coordinates, const String &p_axis, String &r_value) {
	const Variant value = p_coordinates.get(p_axis, Variant());
	if (value.get_type() != Variant::STRING) {
		return false;
	}
	r_value = value;
	return !r_value.is_empty();
}

// The source of one cell. `reordered` declares the carried member after the unrelated one instead of
// before it, so the member set is identical and only its position in the class differs.
static String render_lifecycle_source(
		const LifecycleDestinationShape &p_shape, const String &p_state, bool p_revised) {
	const String spelling = p_revised ? String(lifecycle_revised_spelling) : String(p_shape.spelling);
	const String initializer =
			p_revised ? String(lifecycle_revised_initializer) : String(p_shape.initializer);
	const String carried = vformat("\tvar value: %s = %s\n", spelling, initializer);
	const String unrelated = "\tvar marker: int = 0\n";
	String source = "class Holder:\n";
	source += p_state == "reordered" ? unrelated + carried : carried + unrelated;
	source += "\nfunc test() -> void:\n";
	source += "\tvar holder: Holder = Holder.new()\n";
	source += "\tprint(holder.marker)\n";
	return source;
}

class LifecycleBytecodeResolver : public FSBytecodeExternalResolver {
public:
	virtual Ref<Resource> resolve_resource(const String &) override {
		return Ref<Resource>();
	}

	virtual Ref<Script> resolve_script(const String &, const String &, bool &r_is_local_class) override {
		r_is_local_class = false;
		return Ref<Script>();
	}
};

// Brings the language up for the work this scope covers and puts the process back the way it found
// it. A completeness run reaches this adapter two ways: from a unit test, where an earlier case has
// usually already initialized the language, and from `foundry test completeness run`, where nothing
// has. Compiling without it fails as an internal error rather than as a program that did not compile,
// which is a harness defect reported as a product observation.
//
// The language is process-wide and the registry hands one adapter instance to every run, so the
// decision to bring it up and the decision to take it down are counted under one lock. Testing the
// language and then acting on the answer without the count would let one run tear the language down
// while another was compiling against it.
class LifecycleLanguageBoot {
	static Mutex &boot_mutex() {
		static Mutex mutex;
		return mutex;
	}

	// Live boot scopes, and whether the first of them is the one that initialized the language.
	// Both are read and written only under `boot_mutex`.
	static int &boot_depth() {
		static int depth = 0;
		return depth;
	}

	static bool &boot_owns_language() {
		static bool owns = false;
		return owns;
	}

	static uint64_t &cycles() {
		static uint64_t performed = 0;
		return performed;
	}

public:
	LifecycleLanguageBoot() {
		MutexLock lock(boot_mutex());
		if (boot_depth() == 0) {
			boot_owns_language() = ensure_fs_language_initialized();
		}
		boot_depth()++;
	}

	~LifecycleLanguageBoot() {
		MutexLock lock(boot_mutex());
		boot_depth()--;
		if (boot_depth() == 0 && boot_owns_language()) {
			boot_owns_language() = false;
			FSLanguage::get_singleton()->finish();
		}
	}

	// Takes the language down and brings it back, which is the transition the shutdown family carries a
	// type across. It runs under the same lock the boot count is decided under, so a cycle can never
	// interleave with another run's decision to bring the language up or take it down.
	static void cycle() {
		MutexLock lock(boot_mutex());
		FSLanguage::get_singleton()->finish();
		FSLanguage::get_singleton()->init();
		cycles()++;
	}

	// How many times a transition has taken the language down and brought it back. Read under the same
	// lock it is written under, so a stage that asks for the transition twice is two here and never one.
	static uint64_t observed_cycles() {
		MutexLock lock(boot_mutex());
		return cycles();
	}

	LifecycleLanguageBoot(const LifecycleLanguageBoot &) = delete;
	LifecycleLanguageBoot &operator=(const LifecycleLanguageBoot &) = delete;
};

static void append_diagnostic(FSCompletenessObservation &r_observation, const String &p_code,
		const String &p_message) {
	r_observation.diagnostics.push_back(p_message);
	Dictionary record;
	record["severity"] = "error";
	record["category"] = "lifecycle";
	record["code"] = p_code;
	record["line"] = 0.0;
	record["column"] = 0.0;
	record["message"] = p_message;
	record["suppressed"] = false;
	r_observation.diagnostic_records.push_back(record);
}

// Compiles one source at one identity. The identity is a synthetic path the cache serves the source
// from, which is what lets a transition be exercised without a file on disk.
static Error compile_lifecycle_source(
		const String &p_source, const String &p_path, FSParser &r_parser, Ref<FoundryScript> &r_script) {
	r_script.unref();
	Error error = OK;
	Ref<FoundryScript> script = FSCache::get_shallow_script(p_path, error);
	if (error != OK || script.is_null()) {
		return error == OK ? ERR_CANT_CREATE : error;
	}
	error = r_parser.parse(p_source, p_path, false);
	if (error != OK) {
		return error;
	}
	FSAnalyzer analyzer(&r_parser);
	error = analyzer.analyze();
	if (error != OK) {
		return error;
	}
	FSCompiler compiler;
	error = compiler.compile(&r_parser, script.ptr(), false);
	if (error != OK) {
		return error;
	}
	r_script = script;
	return OK;
}

// The exported form of one compiled script: the artifact the export/load transition carries a type
// through. Held as bytes on purpose - that is exactly what the transition consumes, so a stale or a
// corrupted artifact is a different value here rather than a different code path.
struct LifecycleArtifact {
	Vector<uint8_t> buffer;
	String path;
};

static Error export_lifecycle_artifact(
		const Ref<FoundryScript> &p_script, const String &p_path, LifecycleArtifact &r_artifact) {
	r_artifact = LifecycleArtifact();
	if (p_script.is_null()) {
		return ERR_INVALID_PARAMETER;
	}
	FSBytecodeExporter exporter;
	Vector<uint8_t> buffer;
	const Error error = exporter.serialize(p_script, buffer);
	if (error != OK) {
		return error;
	}
	r_artifact.buffer = buffer;
	r_artifact.path = p_path;
	return OK;
}

static Error load_lifecycle_artifact(const LifecycleArtifact &p_artifact, Ref<FoundryScript> &r_loaded) {
	r_loaded.unref();
	if (p_artifact.buffer.is_empty()) {
		return ERR_INVALID_DATA;
	}
	Ref<FoundryScript> restored;
	restored.instantiate();
	restored->set_path_cache(p_artifact.path);
	LifecycleBytecodeResolver resolver;
	FSBytecodeLoader loader;
	loader.set_resolver(&resolver);
	Error error = loader.load_skeleton(p_artifact.buffer, restored);
	if (error == OK) {
		error = loader.load_full(p_artifact.buffer, restored);
	}
	if (error != OK) {
		return error;
	}
	if (restored.is_null() || !restored->is_valid() || !restored->is_compiled_binary()) {
		return ERR_INVALID_DATA;
	}
	r_loaded = restored;
	return OK;
}

// Replaces what the identity an artifact recorded now serves. The artifact keeps the path it was
// taken at, so rewriting the file behind that path is what makes the artifact stale: a transition
// that reached back to the source, or that re-derived from the identity, hands back the revision
// while one that reads its own bytes does not.
//
// The cache entries for the identity are deliberately left alone. Invalidating them is the work a
// re-deriving transition has to do for itself, and a stage that did it on the transition's behalf
// could not tell a transition that invalidates from one that hands back what it already had.
static Error revise_source_at(const String &p_path, const String &p_source) {
	Error error = OK;
	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::WRITE, &error);
	if (file.is_null()) {
		return error == OK ? ERR_FILE_CANT_WRITE : error;
	}
	file->store_string(p_source);
	file->close();
	return FileAccess::exists(p_path) ? OK : ERR_FILE_CANT_WRITE;
}

// Test seam: reconstructs the artifact by recompiling whatever the path it recorded serves now,
// which is what a loader that resolved a serialized type against current state instead of against
// its own bytes would arrive at. Every stage whose source is unchanged is unaffected; the stale stage,
// whose source was revised behind that path, is exactly the one this has to be visible in.
static Error load_lifecycle_artifact_from_source(
		const LifecycleArtifact &p_artifact, Ref<FoundryScript> &r_loaded) {
	r_loaded.unref();
	Error error = OK;
	const String source = FileAccess::get_file_as_string(p_artifact.path, &error);
	if (error != OK || source.is_empty()) {
		return error == OK ? ERR_FILE_NOT_FOUND : error;
	}
	FSParser parser;
	return compile_lifecycle_source(source, p_artifact.path, parser, r_loaded);
}

// Everything the identity serves goes away: the file behind it and everything the cache holds for it.
// The scope that owns the identity stays, because it is what keeps this observation serialized
// against every other one.
static Error retire_source_at(const String &p_path) {
	FSCache::remove_parser(p_path);
	FSCache::remove_script(p_path);
	FSCache::clear_source_override(p_path);
	if (!FileAccess::exists(p_path)) {
		return OK;
	}
	Ref<DirAccess> filesystem = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	if (filesystem.is_null()) {
		return ERR_UNAVAILABLE;
	}
	return filesystem->remove(p_path);
}

// An artifact no loader can accept. The corruption is applied to the payload rather than to the
// header so the refusal comes from the type payload the transition is being measured on.
static LifecycleArtifact corrupted_artifact(const LifecycleArtifact &p_artifact) {
	LifecycleArtifact corrupted = p_artifact;
	for (int index = corrupted.buffer.size() / 2; index < corrupted.buffer.size(); index++) {
		corrupted.buffer.write[index] = uint8_t(0xFF - corrupted.buffer[index]);
	}
	return corrupted;
}

// The declared type of the carried member as the artifact records it. An empty string means the
// artifact has no such member at all, which is a lost type rather than a preserved one.
static String carried_type_spelling(const Ref<FoundryScript> &p_script) {
	if (p_script.is_null()) {
		return String();
	}
	const Ref<FoundryScript> *holder = p_script->get_subclasses().getptr(SNAME("Holder"));
	if (holder == nullptr || holder->is_null()) {
		return String();
	}
	const FSDataType *carried = (*holder)->find_member_data_type(SNAME("value"));
	if (carried == nullptr) {
		return String();
	}
	return carried->get_source_type_name();
}

static bool spelling_is_erased(const String &p_spelling) {
	return p_spelling.is_empty() || p_spelling == "Variant";
}

// What one family asks of the subsystem it names, and everything that subsystem needs to know about
// the stage it is being asked in. A family is a carry function here rather than a family-shaped copy
// of the whole adapter, so a stage means the same thing in every family and only the subsystem
// differs.
struct LifecycleCarryRequest {
	Ref<FoundryScript> subject;
	// The identity the subject was compiled at. What lives behind it may have been revised since.
	String path;
	// Damage the transition's own output. The failure_recovery stage needs a transition that can be
	// made to produce something unusable, and the corruption seam needs the same thing.
	bool damaged = false;
	// Hand back whatever the subsystem already had instead of doing the work the transition names.
	// This is the fault a re-deriving transition has to be visible against.
	bool skip_invalidation = false;
	// Reconstruct from whatever the identity serves now rather than from the artifact's own bytes.
	bool from_source = false;
	// Apply the transition to its own output once more.
	bool twice = false;
};

// Carries the declared type across one family's transition and renders what came out. An empty
// reading means the transition refused, which is an observation about the transition rather than a
// harness failure.
using LifecycleCarry = Error (*)(const LifecycleCarryRequest &p_request, String &r_after);

static Error carry_bytecode_export_load(const LifecycleCarryRequest &p_request, String &r_after) {
	r_after = String();
	LifecycleArtifact artifact;
	Error error = export_lifecycle_artifact(p_request.subject, p_request.path, artifact);
	if (error != OK) {
		return error;
	}
	if (p_request.damaged) {
		artifact = corrupted_artifact(artifact);
	}
	Ref<FoundryScript> carried;
	error = p_request.from_source ? load_lifecycle_artifact_from_source(artifact, carried)
								  : load_lifecycle_artifact(artifact, carried);
	if (error != OK || carried.is_null()) {
		return OK;
	}
	if (p_request.twice) {
		// A second export and load of what the first one produced. A transition that loses nothing
		// once but loses something the second time is not carrying the type, it is decaying it.
		LifecycleArtifact second;
		error = export_lifecycle_artifact(carried, p_request.path, second);
		Ref<FoundryScript> again;
		if (error == OK) {
			error = load_lifecycle_artifact(second, again);
		}
		if (error != OK || again.is_null()) {
			return OK;
		}
		carried = again;
	}
	r_after = carried_type_spelling(carried);
	return OK;
}

// The declared type as the reflection surface describes it. A property record spells a Variant type,
// a class name, and a hint, so what survives the projection is exactly what this renders.
static String render_property_info(const PropertyInfo &p_property) {
	if (p_property.type == Variant::NIL) {
		return "Variant";
	}
	const String base = p_property.class_name != StringName() ? String(p_property.class_name)
															  : String(Variant::get_type_name(p_property.type));
	return p_property.hint_string.is_empty() ? base : base + "[" + p_property.hint_string + "]";
}

static Error carry_proxy_reflection(const LifecycleCarryRequest &p_request, String &r_after) {
	r_after = String();
	if (p_request.subject.is_null()) {
		return ERR_INVALID_PARAMETER;
	}
	const Ref<FoundryScript> *holder = p_request.subject->get_subclasses().getptr(SNAME("Holder"));
	if (holder == nullptr || holder->is_null()) {
		return ERR_INVALID_DATA;
	}
	const auto describe = [&holder](String &r_description) -> Error {
		List<PropertyInfo> properties;
		(*holder)->get_script_property_list(&properties);
		for (const PropertyInfo &property : properties) {
			if (property.name == SNAME("value")) {
				r_description = render_property_info(property);
				return OK;
			}
		}
		return ERR_DOES_NOT_EXIST;
	};
	if (p_request.damaged) {
		// A projection that carried nothing: what the surface would describe if the declared type had
		// not reached it at all.
		r_after = render_property_info(PropertyInfo());
		return OK;
	}
	Error error = describe(r_after);
	if (error != OK) {
		r_after = String();
		return OK;
	}
	if (p_request.twice) {
		// Describing the same member twice has to describe the same type, or the surface is not a
		// projection of the declared type but a function of when it was asked.
		String again;
		error = describe(again);
		if (error != OK || again != r_after) {
			r_after = String();
		}
	}
	return OK;
}

// The identity is compiled again, which is what a reload and a cache replacement both come down to.
// `p_through_cache` decides where the reading is taken from: the object the compiler wrote, or what
// the cache hands back for the identity afterwards.
static Error carry_by_recompiling(const LifecycleCarryRequest &p_request, bool p_through_cache,
		void (*p_before_each_pass)(), String &r_after) {
	r_after = String();
	if (p_request.skip_invalidation) {
		// The subsystem hands back what it already had for the identity instead of re-deriving it.
		// Nothing else about the cell changes, so a stage whose identity now serves a different
		// declaration is the one this is visible in.
		Error cache_error = OK;
		const Ref<FoundryScript> cached = FSCache::get_shallow_script(p_request.path, cache_error);
		if (cache_error != OK || cached.is_null()) {
			return cache_error == OK ? ERR_INVALID_DATA : cache_error;
		}
		r_after = carried_type_spelling(cached);
		return OK;
	}
	// Applying the transition to its own output means the whole transition again, including whatever
	// the family does before re-deriving. A second pass that skipped that would report one shutdown
	// followed by two recompilations as if the subsystem had been taken down twice.
	const int passes = p_request.twice ? 2 : 1;
	for (int pass = 0; pass < passes; pass++) {
		if (p_before_each_pass != nullptr) {
			p_before_each_pass();
		}
		FSCache::remove_parser(p_request.path);
		FSCache::remove_script(p_request.path);
		String source = "class Holder:\n\tvar value: NoSuchTypeExists = 0\n\tvar marker: int = 0\n";
		if (!p_request.damaged) {
			Error read_error = OK;
			source = FileAccess::get_file_as_string(p_request.path, &read_error);
			if (read_error != OK || source.is_empty()) {
				return OK;
			}
		}
		FSParser parser;
		Ref<FoundryScript> reloaded;
		const Error error = compile_lifecycle_source(source, p_request.path, parser, reloaded);
		if (error != OK || reloaded.is_null()) {
			return OK;
		}
		if (p_through_cache) {
			Error cache_error = OK;
			const Ref<FoundryScript> cached = FSCache::get_shallow_script(p_request.path, cache_error);
			if (cache_error != OK || cached.is_null()) {
				return cache_error == OK ? ERR_INVALID_DATA : cache_error;
			}
			reloaded = cached;
		}
		r_after = carried_type_spelling(reloaded);
	}
	return OK;
}

static Error carry_reload(const LifecycleCarryRequest &p_request, String &r_after) {
	return carry_by_recompiling(p_request, false, nullptr, r_after);
}

static Error carry_cache_replacement(const LifecycleCarryRequest &p_request, String &r_after) {
	return carry_by_recompiling(p_request, true, nullptr, r_after);
}

static Error carry_shutdown_reinitialization(const LifecycleCarryRequest &p_request, String &r_after) {
	// Everything the language holds for the identity goes down with it before each pass, so what comes
	// back can only have been rebuilt from the source rather than remembered.
	return carry_by_recompiling(p_request, false, LifecycleLanguageBoot::cycle, r_after);
}

struct LifecycleFamilyShape {
	const char *family;
	LifecycleCarry carry;
};

static const LifecycleFamilyShape lifecycle_family_shapes[] = {
	{ "lifecycle_bytecode_export_load", carry_bytecode_export_load },
	{ "lifecycle_cache_replacement", carry_cache_replacement },
	{ "lifecycle_proxy_reflection", carry_proxy_reflection },
	{ "lifecycle_reload", carry_reload },
	{ "lifecycle_shutdown_reinitialization", carry_shutdown_reinitialization },
};

// The family a rendered program was derived under. A program carries its coordinates and its identity
// but never its family, so the identity is what says which one it belongs to: a case ID is the
// canonical identity of its coordinates under exactly one family.
static String family_of_program(const FSCompletenessProgram &p_program) {
	for (const LifecycleFamilyShape &shape : lifecycle_family_shapes) {
		if (p_program.case_id == FSCompletenessCaseID::make(shape.family, p_program.coordinates)) {
			return shape.family;
		}
	}
	return String();
}

static const LifecycleFamilyShape *find_family_shape(const String &p_family) {
	for (const LifecycleFamilyShape &shape : lifecycle_family_shapes) {
		if (p_family == shape.family) {
			return &shape;
		}
	}
	return nullptr;
}

// The one place a pair of spellings becomes an outcome. A cell never derives this from its
// coordinates: both spellings are read off artifacts the transition produced.
static String classify_semantic_identity(const String &p_before, const String &p_after) {
	if (p_after.is_empty()) {
		return "rejected";
	}
	if (p_before == p_after) {
		return "preserved";
	}
	return spelling_is_erased(p_after) && !spelling_is_erased(p_before) ? "erased" : "projected";
}

} // namespace

void LifecycleInternal::set_corrupt_transition_artifact_for_test(bool p_corrupt) {
	corrupt_transition_artifact_for_test = p_corrupt;
}

void LifecycleInternal::set_load_transition_artifact_from_source_for_test(bool p_load_from_source) {
	load_transition_artifact_from_source_for_test = p_load_from_source;
}

void LifecycleInternal::set_skip_transition_invalidation_for_test(bool p_skip) {
	skip_transition_invalidation_for_test = p_skip;
}

uint64_t LifecycleInternal::language_cycles_for_test() {
	return LifecycleLanguageBoot::observed_cycles();
}

Vector<String> lifecycle_destinations() {
	Vector<String> leaves;
	for (const LifecycleDestinationShape &shape : lifecycle_destination_shapes) {
		leaves.push_back(shape.leaf);
	}
	return leaves;
}

Vector<String> lifecycle_states() {
	return Vector<String>(
			{ "clean", "incremental", "stale", "missing", "reordered", "failure_recovery" });
}

const FSLifecycleAdapter &FSLifecycleAdapter::shared() {
	static FSLifecycleAdapter adapter;
	return adapter;
}

Vector<String> FSLifecycleAdapter::families() {
	Vector<String> registered;
	for (const LifecycleFamilyShape &shape : lifecycle_family_shapes) {
		registered.push_back(shape.family);
	}
	registered.sort();
	return registered;
}

String FSLifecycleAdapter::id() const {
	return lifecycle_adapter_id;
}

HashSet<String> FSLifecycleAdapter::observable_dimensions() const {
	return HashSet<String>({ "semantic_identity", "transition_outcome" });
}

HashMap<String, Vector<String>> FSLifecycleAdapter::renderable_leaves() const {
	HashMap<String, Vector<String>> leaves;
	leaves["destination"] = lifecycle_destinations();
	leaves["lifecycle_state"] = lifecycle_states();
	leaves["surface"] = Vector<String>({ "text", "bytecode" });
	return leaves;
}

Error FSLifecycleAdapter::render(
		const FSCompletenessResolvedCell &p_cell, FSCompletenessProgram &r_program) const {
	r_program = FSCompletenessProgram();
	String destination;
	String state;
	String surface;
	if (p_cell.case_id.is_empty() || !read_coordinate(p_cell.coordinates, "destination", destination) ||
			!read_coordinate(p_cell.coordinates, "lifecycle_state", state) ||
			!read_coordinate(p_cell.coordinates, "surface", surface)) {
		return ERR_INVALID_DATA;
	}
	if (!can_render("destination", destination) || !can_render("lifecycle_state", state) ||
			!can_render("surface", surface)) {
		return ERR_INVALID_DATA;
	}
	const LifecycleDestinationShape *shape = find_destination_shape(destination);
	if (shape == nullptr) {
		return ERR_INVALID_DATA;
	}
	FSCompletenessProgram program;
	program.case_id = p_cell.case_id;
	program.surface = surface;
	program.coordinates = p_cell.coordinates.duplicate();
	program.source = render_lifecycle_source(*shape, state, false);
	// The evidence of a lifecycle cell is the type the transition handed back, not what the program
	// printed, so the program is never run and both sides of the output comparison are empty.
	program.expected_output = String();
	r_program = program;
	return OK;
}

FSCompletenessObservation FSLifecycleAdapter::analyze(
		const FSCompletenessProgram &p_program, const String &p_surface) const {
	FSCompletenessObservation observation;
	observation.case_id = p_program.case_id;
	observation.surface = p_surface;
	if (p_program.surface != p_surface) {
		append_diagnostic(observation, "surface_mismatch",
				vformat("Program surface '%s' does not match observed surface '%s'.", p_program.surface,
						p_surface));
		return observation;
	}
	return observe_transition(p_program, family_of_program(p_program), nullptr);
}

FSCompletenessObservation FSLifecycleAdapter::observe_transition(const FSCompletenessProgram &p_program,
		const String &p_family, Error *r_structural_error) const {
	if (r_structural_error != nullptr) {
		*r_structural_error = OK;
	}
	FSCompletenessObservation observation;
	observation.case_id = p_program.case_id;
	observation.surface = p_program.surface;
	const LifecycleFamilyShape *family = find_family_shape(p_family);
	if (family == nullptr) {
		append_diagnostic(observation, "family_unknown",
				vformat("Family '%s' names no lifecycle transition.", p_family));
		if (r_structural_error != nullptr) {
			*r_structural_error = ERR_INVALID_DATA;
		}
		return observation;
	}
	String destination;
	String state;
	if (!read_coordinate(p_program.coordinates, "destination", destination) ||
			!read_coordinate(p_program.coordinates, "lifecycle_state", state)) {
		append_diagnostic(observation, "coordinates_unreadable", "Program coordinates are unreadable.");
		if (r_structural_error != nullptr) {
			*r_structural_error = ERR_INVALID_DATA;
		}
		return observation;
	}
	const LifecycleDestinationShape *shape = find_destination_shape(destination);
	if (shape == nullptr) {
		append_diagnostic(observation, "destination_unknown",
				vformat("Destination '%s' has no rendered shape.", destination));
		if (r_structural_error != nullptr) {
			*r_structural_error = ERR_INVALID_DATA;
		}
		return observation;
	}

	LifecycleLanguageBoot language;
	// The scope is held for the whole observation. It is what serializes this run against every other
	// user of a synthetic source, and a transition that takes the language down needs that: giving the
	// scope up early would let a reinitialization clear a script another run was still reading.
	DestinationWrapperInternal::SyntheticSourceScope synthetic_source(
			"lifecycle_" + p_program.case_id, p_program.source);
	if (!synthetic_source.is_available()) {
		append_diagnostic(observation, "identity_unavailable", "Lifecycle source identity is unavailable.");
		if (r_structural_error != nullptr) {
			*r_structural_error = ERR_CANT_CREATE;
		}
		return observation;
	}
	const String path = synthetic_source.get_path();

	FSParser parser;
	Ref<FoundryScript> compiled;
	Error error = compile_lifecycle_source(p_program.source, path, parser, compiled);
	if (error != OK) {
		append_diagnostic(observation, "compilation_failed",
				vformat("Lifecycle program did not compile (error %d).", error));
		if (r_structural_error != nullptr) {
			*r_structural_error = error;
		}
		return observation;
	}

	// What the transition has to carry is the type the source declared, so the reading it is compared
	// against is taken from the freshly compiled script whatever surface the cell runs on. Taking it
	// off the transition's own input instead would compare a round trip against its own output, which
	// is blind to an erasure that is already in the artifact it started from.
	const String before = carried_type_spelling(compiled);
	if (before.is_empty()) {
		append_diagnostic(observation, "carried_member_absent",
				"The compiled program does not carry the member the transition is measured on.");
		if (r_structural_error != nullptr) {
			*r_structural_error = ERR_INVALID_DATA;
		}
		return observation;
	}

	// The subject is the artifact the transition is applied to. On the bytecode surface it has already
	// been through one export and load, so a family that claims a type survives its transition has to
	// say so about a restored artifact as well as about a freshly compiled one.
	Ref<FoundryScript> subject = compiled;
	if (p_program.surface == "bytecode") {
		LifecycleArtifact staged;
		error = export_lifecycle_artifact(compiled, path, staged);
		if (error == OK) {
			error = load_lifecycle_artifact(staged, subject);
		}
		if (error != OK || subject.is_null()) {
			append_diagnostic(observation, "surface_unavailable",
					vformat("Lifecycle subject could not be restored for the bytecode surface (error %d).",
							error));
			if (r_structural_error != nullptr) {
				*r_structural_error = error == OK ? ERR_INVALID_DATA : error;
			}
			return observation;
		}
	}

	// What makes a stage a stage is the state the transition's input is in, never a different way of
	// reading the result.
	String outcome = "completed";
	if (state == "stale") {
		// The source behind the very identity the subject was compiled at is revised, so that identity
		// now declares a type the subject does not. A transition that reads its own bytes still carries
		// the original; one that re-derives from the identity carries the revision. Both are correct
		// answers to different questions, and which one a family gives is exactly what this stage pins.
		const String revised = render_lifecycle_source(*shape, state, true);
		{
			// The revision is only usable as evidence if it declares something the original did not,
			// and that is checked at an identity of its own so the cache entry behind `path` keeps
			// describing what was compiled there.
			DestinationWrapperInternal::SyntheticSourceScope probe(
					"lifecycle_revision_probe_" + p_program.case_id, revised);
			FSParser probe_parser;
			Ref<FoundryScript> probe_script;
			error = probe.is_available()
					? compile_lifecycle_source(revised, probe.get_path(), probe_parser, probe_script)
					: ERR_CANT_CREATE;
			if (error != OK) {
				append_diagnostic(observation, "revision_failed",
						vformat("The revised lifecycle program did not compile (error %d).", error));
				if (r_structural_error != nullptr) {
					*r_structural_error = error;
				}
				return observation;
			}
			if (carried_type_spelling(probe_script) == before) {
				append_diagnostic(observation, "revision_indistinguishable",
						"The revised program declares the same type, so a stale identity cannot be told "
						"apart.");
				if (r_structural_error != nullptr) {
					*r_structural_error = ERR_INVALID_DATA;
				}
				return observation;
			}
		}
		error = revise_source_at(path, revised);
		if (error != OK) {
			append_diagnostic(observation, "revision_failed",
					vformat("The lifecycle source could not be revised in place (error %d).", error));
			if (r_structural_error != nullptr) {
				*r_structural_error = error;
			}
			return observation;
		}
	}

	LifecycleCarryRequest request;
	request.subject = subject;
	request.path = path;
	request.from_source = load_transition_artifact_from_source_for_test;
	request.skip_invalidation = skip_transition_invalidation_for_test;
	request.twice = state == "incremental";

	if (state == "failure_recovery") {
		// The recovery only proves something if the attempt it recovers from produced something else.
		LifecycleCarryRequest damaged = request;
		damaged.damaged = true;
		damaged.twice = false;
		String damaged_reading;
		const Error damage_error = family->carry(damaged, damaged_reading);
		if (damage_error != OK) {
			append_diagnostic(observation, "recovery_unavailable",
					vformat("The damaged attempt could not be carried out (error %d).", damage_error));
			if (r_structural_error != nullptr) {
				*r_structural_error = damage_error;
			}
			return observation;
		}
		if (!damaged_reading.is_empty() && damaged_reading == before) {
			append_diagnostic(observation, "recovery_unproven",
					"The damaged transition carried the declared type anyway, so the recovery proves "
					"nothing.");
			if (r_structural_error != nullptr) {
				*r_structural_error = ERR_INVALID_DATA;
			}
			return observation;
		}
		outcome = "recovered";
	}
	if (state == "missing") {
		// The identity the subject was compiled at stops serving anything before the transition runs,
		// so nothing it carries may come from a source anyone can still read.
		error = retire_source_at(path);
		if (error != OK) {
			append_diagnostic(observation, "retirement_failed",
					vformat("The lifecycle source could not be retired (error %d).", error));
			if (r_structural_error != nullptr) {
				*r_structural_error = error;
			}
			return observation;
		}
	}

	request.damaged = corrupt_transition_artifact_for_test;
	String after;
	error = family->carry(request, after);
	if (error != OK) {
		append_diagnostic(observation, "transition_failed",
				vformat("The lifecycle transition could not be carried out (error %d).", error));
		if (r_structural_error != nullptr) {
			*r_structural_error = error;
		}
		return observation;
	}
	observation.dimensions["semantic_identity"] = classify_semantic_identity(before, after);
	observation.dimensions["transition_outcome"] = after.is_empty() ? "refused" : outcome;
	observation.produced_output = String();
	return observation;
}

Error FSLifecycleAdapter::execute(const String &p_scratch_root,
		const Vector<FSCompletenessProgram> &p_programs, FSCompletenessRuntimeBatch &r_batch) const {
	r_batch = FSCompletenessRuntimeBatch();
	if (p_scratch_root.is_empty()) {
		return ERR_INVALID_PARAMETER;
	}
	// A rendered program carries its coordinates and its identity but never its family, so the first
	// program decides which family the batch belongs to and every later one has to agree. A batch
	// mixing two families would otherwise pass an identity check that only asked "canonical under some
	// family".
	String batch_family;
	// One boot for the whole batch: every program in it compiles, so bringing the language up and down
	// per program would pay for it sixty times over.
	LifecycleLanguageBoot language;
	FSCompletenessRuntimeBatch completed;
	for (const FSCompletenessProgram &program : p_programs) {
		if (program.case_id.is_empty() || program.source.is_empty()) {
			return ERR_INVALID_DATA;
		}
		if (batch_family.is_empty()) {
			batch_family = family_of_program(program);
			if (batch_family.is_empty()) {
				return ERR_INVALID_DATA;
			}
		} else if (program.case_id != FSCompletenessCaseID::make(batch_family, program.coordinates)) {
			return ERR_INVALID_DATA;
		}
		HashMap<String, FSCompletenessRuntimeResult> *results =
				completed.results_for_surface(program.surface);
		if (results == nullptr || results->has(program.case_id)) {
			return results == nullptr ? ERR_INVALID_DATA : ERR_ALREADY_IN_USE;
		}
		Error structural_error = OK;
		FSCompletenessObservation observation =
				observe_transition(program, batch_family, &structural_error);
		if (structural_error != OK) {
			return structural_error;
		}
		FSCompletenessRuntimeResult result;
		static_cast<FSCompletenessObservation &>(result) = observation;
		result.passed = result.diagnostics.is_empty();
		result.status = result.passed ? "ok" : "transition_rejected";
		results->insert(program.case_id, result);
	}
	r_batch = completed;
	return OK;
}

} // namespace FSTests
