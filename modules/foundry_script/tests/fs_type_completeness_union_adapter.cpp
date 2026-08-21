/**************************************************************************/
/*  fs_type_completeness_union_adapter.cpp                               */
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

#include "fs_type_completeness_union_adapter.h"
#include "fs_temporary_project_tree.h"
#include "fs_test_runner.h"

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
#include "core/os/os.h"
#include "core/templates/safe_refcount.h"

namespace FSTests {

namespace {

static const String union_completeness_family = "union_destination_membership";

static bool is_allowed_coordinate(const String &p_axis, const String &p_value) {
	if (p_axis == "destination") {
		return p_value == "plain" || p_value == "union";
	}
	if (p_axis == "source_proof") {
		return p_value == "static_member" || p_value == "numeric_constant" || p_value == "gradual" ||
				p_value == "erased" || p_value == "variant";
	}
	if (p_axis == "boundary") {
		return p_value == "argument_binding" || p_value == "reflective_write";
	}
	if (p_axis == "surface") {
		return p_value == "text" || p_value == "bytecode";
	}
	return false;
}

static bool read_coordinate(const Dictionary &p_coordinates, const String &p_axis, String &r_value) {
	const Variant value = p_coordinates.get(p_axis, Variant());
	if (value.get_type() != Variant::STRING) {
		return false;
	}
	r_value = value;
	return is_allowed_coordinate(p_axis, r_value);
}

static String source_expression_for(const String &p_source_proof) {
	if (p_source_proof == "static_member") {
		return "typed_source";
	}
	if (p_source_proof == "numeric_constant") {
		return "5";
	}
	if (p_source_proof == "gradual") {
		return "supply(5U)";
	}
	if (p_source_proof == "erased") {
		return "erase[uint](5U)";
	}
	return "variant_source";
}

static String boundary_body_for(const String &p_boundary) {
	if (p_boundary == "argument_binding") {
		return "\t@warning_ignore(\"unsafe_call_argument\")\n"
			   "\tvar stored: Variant = accept(SOURCE)";
	}
	return "\tvar holder := Holder.new()\n"
		   "\t@warning_ignore(\"unsafe_call_argument\")\n"
		   "\tholder.set(&\"value\", SOURCE)\n"
		   "\tvar stored: Variant = holder.value";
}

static void append_parser_diagnostics_in_source_order(const FSParser &p_parser, PackedStringArray &r_diagnostics) {
	for (const FSParser::ParserError *error : p_parser.get_errors_in_source_order()) {
		if (error != nullptr) {
			r_diagnostics.push_back(vformat("%d:%d: %s", error->line, error->column, error->message));
		}
	}
}

static Mutex &synthetic_source_mutex() {
	static Mutex mutex;
	return mutex;
}

static String next_synthetic_source_tree_name() {
	static SafeNumeric<uint64_t> sequence;
	return vformat("fstc_%d_%s", OS::get_singleton()->get_process_id(), String::num_uint64(sequence.increment()));
}

static FSCompletenessObservation rejected_observation(
		const FSCompletenessProgram &p_program, const String &p_surface, const String &p_diagnostic) {
	FSCompletenessObservation observation;
	observation.case_id = p_program.case_id;
	observation.surface = p_surface;
	observation.dimensions["analysis"] = "reject";
	observation.diagnostics.push_back(p_diagnostic);
	return observation;
}

static bool is_safe_case_id(const String &p_case_id) {
	if (p_case_id.is_empty()) {
		return false;
	}
	for (int i = 0; i < p_case_id.length(); i++) {
		const char32_t character = p_case_id[i];
		if (!((character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
					(character >= '0' && character <= '9') || character == '-' || character == '_')) {
			return false;
		}
	}
	return true;
}

static bool read_program_coordinates(const FSCompletenessProgram &p_program, String &r_destination,
		String &r_source_proof, String &r_boundary, String &r_surface) {
	return p_program.coordinates.size() == 4 &&
			read_coordinate(p_program.coordinates, "destination", r_destination) &&
			read_coordinate(p_program.coordinates, "source_proof", r_source_proof) &&
			read_coordinate(p_program.coordinates, "boundary", r_boundary) &&
			read_coordinate(p_program.coordinates, "surface", r_surface);
}

static String semantic_pair_key(const Dictionary &p_coordinates) {
	Dictionary semantic_coordinates = p_coordinates.duplicate();
	semantic_coordinates.erase("surface");
	return FSCompletenessCaseID::canonical_coordinates(semantic_coordinates);
}

enum RuntimeDestinationKind {
	RUNTIME_DESTINATION_DIRECTORY,
	RUNTIME_DESTINATION_FILE,
};

struct RuntimeDestination {
	String path;
	RuntimeDestinationKind kind = RUNTIME_DESTINATION_FILE;
};

static Error reject_symlink_aliases(const String &p_container_root, const String &p_path) {
	const String container_root = p_container_root.simplify_path();
	String current = p_path.simplify_path();
	if (current != container_root &&
			!TemporaryProjectTree::is_strict_descendant(container_root, current)) {
		return ERR_UNAUTHORIZED;
	}

	Ref<DirAccess> filesystem = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	if (filesystem.is_null()) {
		return ERR_CANT_OPEN;
	}
	while (true) {
		if (filesystem->is_link(current)) {
			return ERR_UNAUTHORIZED;
		}
		if (current == container_root) {
			return OK;
		}
		const String parent = current.get_base_dir().simplify_path();
		if (parent.is_empty() || parent == current) {
			return ERR_UNAUTHORIZED;
		}
		current = parent;
	}
}

static Error preflight_runtime_destination(
		const String &p_runtime_root, const RuntimeDestination &p_destination) {
	if (!TemporaryProjectTree::is_strict_descendant(p_runtime_root, p_destination.path)) {
		return ERR_UNAUTHORIZED;
	}
	const Error alias_error = reject_symlink_aliases(p_runtime_root, p_destination.path);
	if (alias_error != OK) {
		return alias_error;
	}

	Ref<DirAccess> filesystem = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	if (filesystem.is_null()) {
		return ERR_CANT_OPEN;
	}
	const bool directory_exists = filesystem->dir_exists(p_destination.path);
	const bool file_exists = filesystem->file_exists(p_destination.path);
	if (!directory_exists && !file_exists) {
		return OK;
	}

	String canonical_existing_path;
	const Error resolve_error = TemporaryProjectTree::resolve_existing_owned_path(
			p_destination.path, canonical_existing_path);
	if (resolve_error != OK) {
		return resolve_error;
	}
	if (!TemporaryProjectTree::is_strict_descendant(p_runtime_root, canonical_existing_path)) {
		return ERR_UNAUTHORIZED;
	}
	if (p_destination.kind == RUNTIME_DESTINATION_DIRECTORY && !directory_exists) {
		return ERR_CANT_CREATE;
	}
	if (p_destination.kind == RUNTIME_DESTINATION_FILE && !file_exists) {
		return ERR_CANT_CREATE;
	}
	return OK;
}

static Error write_runtime_file(const String &p_path, const String &p_contents) {
	Ref<DirAccess> directory = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	if (directory.is_null()) {
		return ERR_CANT_CREATE;
	}
	if (directory->is_link(p_path.get_base_dir()) || directory->is_link(p_path)) {
		return ERR_UNAUTHORIZED;
	}
	const Error directory_error = directory->make_dir_recursive(p_path.get_base_dir());
	if (directory_error != OK) {
		return directory_error;
	}
	Error file_error = OK;
	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::WRITE, &file_error);
	if (file.is_null()) {
		return file_error == OK ? ERR_CANT_CREATE : file_error;
	}
	file->store_string(p_contents);
	return file->get_error();
}

#ifdef TOOLS_ENABLED
class UnionCompletenessBytecodeResolver : public FSBytecodeExternalResolver {
public:
	virtual Ref<Resource> resolve_resource(const String &) override {
		return Ref<Resource>();
	}

	virtual Ref<Script> resolve_script(const String &, const String &, bool &r_is_local_class) override {
		r_is_local_class = false;
		return Ref<Script>();
	}
};
#endif

static Error compile_runtime_contract_script(const FSCompletenessProgram &p_program,
		Ref<FoundryScript> &r_original, Ref<FoundryScript> &r_inspected, PackedStringArray &r_diagnostics) {
	r_original.unref();
	r_inspected.unref();
	UnionCompletenessInternal::SyntheticSourceScope synthetic_source(
			"runtime_contract_" + p_program.case_id, p_program.source);
	if (!synthetic_source.is_available()) {
		r_diagnostics.push_back("Runtime contract source identity is unavailable.");
		return ERR_CANT_CREATE;
	}

	Error error = OK;
	r_original = FSCache::get_shallow_script(synthetic_source.get_path(), error);
	if (error != OK || r_original.is_null()) {
		r_diagnostics.push_back(vformat("Runtime contract script setup failed (error %d).", error));
		return error == OK ? ERR_CANT_CREATE : error;
	}

	FSParser parser;
	error = parser.parse(p_program.source, synthetic_source.get_path(), false);
	if (error == OK) {
		FSAnalyzer analyzer(&parser);
		error = analyzer.analyze();
		if (error == OK) {
			FSCompiler compiler;
			error = compiler.compile(&parser, r_original.ptr(), false);
		}
	}
	append_parser_diagnostics_in_source_order(parser, r_diagnostics);
	if (error != OK) {
		if (r_diagnostics.is_empty()) {
			r_diagnostics.push_back(vformat("Runtime contract compilation failed (error %d).", error));
		}
		return error;
	}

	r_inspected = r_original;
	if (p_program.surface == "bytecode") {
#ifdef TOOLS_ENABLED
		FSBytecodeExporter exporter;
		Vector<uint8_t> buffer;
		error = exporter.serialize(r_original, buffer);
		if (error != OK) {
			r_diagnostics.push_back(vformat("Runtime contract serialization failed (error %d).", error));
			return error;
		}

		Ref<FoundryScript> restored;
		restored.instantiate();
		restored->set_path_cache(r_original->get_script_path());
		UnionCompletenessBytecodeResolver resolver;
		FSBytecodeLoader loader;
		loader.set_resolver(&resolver);
		error = loader.load_skeleton(buffer, restored);
		if (error == OK) {
			error = loader.load_full(buffer, restored);
		}
		if (error != OK || restored.is_null() || !restored->is_valid() || !restored->is_compiled_binary() ||
				restored.ptr() == r_original.ptr()) {
			r_diagnostics.push_back(vformat("Runtime contract bytecode reload failed (error %d).", error));
			return error == OK ? ERR_INVALID_DATA : error;
		}
		r_inspected = restored;
#else
		r_diagnostics.push_back("Runtime contract bytecode reload is unavailable in this build.");
		return ERR_UNAVAILABLE;
#endif
	}
	return OK;
}

static bool descriptor_is_expected_destination(const FSDataType &p_descriptor, const String &p_destination) {
	if (p_destination == "plain") {
		return p_descriptor.kind == FSDataType::BUILTIN && p_descriptor.builtin_type == Variant::UINT;
	}
	if (p_descriptor.kind != FSDataType::UNION || p_descriptor.union_alternatives.size() != 2) {
		return false;
	}
	bool has_uint = false;
	bool has_string = false;
	for (const FSDataType &alternative : p_descriptor.union_alternatives) {
		has_uint = has_uint ||
				(alternative.kind == FSDataType::BUILTIN && alternative.builtin_type == Variant::UINT);
		has_string = has_string ||
				(alternative.kind == FSDataType::BUILTIN && alternative.builtin_type == Variant::STRING);
	}
	return has_uint && has_string;
}

static const FSDataType *find_runtime_destination_descriptor(const Ref<FoundryScript> &p_script,
		const String &p_boundary, PackedStringArray &r_diagnostics) {
	if (p_boundary == "argument_binding") {
		FSFunction *const *accept = p_script->get_member_functions().getptr(SNAME("accept"));
		if (accept == nullptr || *accept == nullptr || (*accept)->get_argument_count() != 1) {
			r_diagnostics.push_back("Compiled accept function does not expose one destination argument.");
			return nullptr;
		}
		return &(*accept)->get_argument_type(0);
	}

	const Ref<FoundryScript> *holder = p_script->get_subclasses().getptr(SNAME("Holder"));
	if (holder == nullptr || holder->is_null()) {
		r_diagnostics.push_back("Compiled Holder class is unavailable.");
		return nullptr;
	}
	const FSDataType *descriptor = (*holder)->find_member_data_type(SNAME("value"));
	if (descriptor == nullptr) {
		r_diagnostics.push_back("Compiled Holder.value descriptor is unavailable.");
	}
	return descriptor;
}

static bool observations_match_on_common_dimensions(
		const FSCompletenessRuntimeResult &p_text, const FSCompletenessRuntimeResult &p_bytecode) {
	if (p_text.produced_output != p_bytecode.produced_output) {
		return false;
	}
	for (const Variant &key : p_text.dimensions.keys()) {
		if (p_bytecode.dimensions.has(key) && p_text.dimensions[key] != p_bytecode.dimensions[key]) {
			return false;
		}
	}
	return true;
}

static Error extract_program_output(const FSTestRunner::FixtureOutcome &p_outcome, String &r_output) {
	r_output = String();
	const String status_line = "FS_TEST_OK\n";
	if (p_outcome.status != "ok" || !p_outcome.output.begins_with(status_line)) {
		return ERR_INVALID_DATA;
	}
	r_output = p_outcome.output.trim_prefix(status_line);
	return OK;
}

} // namespace

UnionCompletenessInternal::SyntheticSourceScope::SyntheticSourceScope(
		const String &p_identity, const String &p_source) {
	synthetic_source_mutex().lock();
	lock_held = true;
	tree = memnew(TemporaryProjectTree(next_synthetic_source_tree_name()));
	if (!tree->is_valid()) {
		return;
	}

	const String filename = p_identity.sha256_text() + ".fs";
	path = tree->root.path_join(filename);
	tree->write_file(filename, p_source);
	if (!FileAccess::exists(path)) {
		return;
	}
	FSCache::clear_source_override(path);
	FSCache::remove_parser(path);
	FSCache::remove_script(path);
	source_available = true;
}

UnionCompletenessInternal::SyntheticSourceScope::~SyntheticSourceScope() {
	if (!path.is_empty()) {
		FSCache::remove_parser(path);
		FSCache::remove_script(path);
		FSCache::clear_source_override(path);
	}
	if (tree != nullptr) {
		memdelete(tree);
	}
	if (lock_held) {
		synthetic_source_mutex().unlock();
	}
}

Error FSUnionCompletenessAdapter::render(
		const FSCompletenessResolvedCell &p_cell, FSCompletenessProgram &r_program) {
	r_program = FSCompletenessProgram();
	if (p_cell.case_id.is_empty() || p_cell.coordinates.size() != 4) {
		return ERR_INVALID_DATA;
	}

	String destination;
	String source_proof;
	String boundary;
	String surface;
	if (!read_coordinate(p_cell.coordinates, "destination", destination) ||
			!read_coordinate(p_cell.coordinates, "source_proof", source_proof) ||
			!read_coordinate(p_cell.coordinates, "boundary", boundary) ||
			!read_coordinate(p_cell.coordinates, "surface", surface)) {
		return ERR_INVALID_DATA;
	}

	const String destination_type = destination == "plain" ? "uint" : "uint | String";
	const String source_expression = source_expression_for(source_proof);
	const String boundary_body = boundary_body_for(boundary).replace("SOURCE", source_expression);
	String source = R"FS(class Holder extends RefCounted:
	var value: DESTINATION = 0U

func supply(value):
	return value

func erase[T](value: T) -> Variant:
	return value

func accept(value: DESTINATION) -> Variant:
	return value

func carrier_of(value: Variant) -> String:
	if value is uint:
		return "uint " + str(value)
	if value is int:
		return "int " + str(value)
	return "other"

func test() -> void:
	var typed_source: uint = 5U
	var variant_source: Variant = 5U
	var _source_witnesses: Variant = [typed_source, variant_source]
BOUNDARY_BODY
	print(carrier_of(stored))
)FS";
	source = source.replace("DESTINATION", destination_type).replace("BOUNDARY_BODY", boundary_body);

	FSCompletenessProgram program;
	program.case_id = p_cell.case_id;
	program.surface = surface;
	program.coordinates = p_cell.coordinates.duplicate();
	program.source = source;
	program.expected_output = "uint 5\n";
	r_program = program;
	return OK;
}

FSCompletenessObservation FSUnionCompletenessAdapter::analyze(
		const FSCompletenessProgram &p_program, const String &p_surface) {
	if (p_surface != "text" && p_surface != "bytecode") {
		return rejected_observation(p_program, p_surface, vformat("Unknown surface '%s'.", p_surface));
	}
	if (p_program.surface != p_surface) {
		return rejected_observation(p_program, p_surface,
				vformat("Program surface '%s' does not match observed surface '%s'.", p_program.surface, p_surface));
	}
	if (p_program.case_id.is_empty()) {
		return rejected_observation(p_program, p_surface, "Program case ID is empty.");
	}

	FSCompletenessObservation observation;
	observation.case_id = p_program.case_id;
	observation.surface = p_surface;
	UnionCompletenessInternal::SyntheticSourceScope synthetic_source(p_program.case_id, p_program.source);
	if (!synthetic_source.is_available()) {
		observation.dimensions["analysis"] = "reject";
		observation.diagnostics.push_back("In-memory analyzer identity is unavailable.");
		return observation;
	}
	const String &path = synthetic_source.get_path();

	FSParser parser;
	const Error parse_error = parser.parse(p_program.source, path, false);
	Error analyzer_error = ERR_PARSE_ERROR;
	if (parse_error == OK) {
		FSAnalyzer analyzer(&parser);
		analyzer_error = analyzer.analyze();
	}
	append_parser_diagnostics_in_source_order(parser, observation.diagnostics);
	if (parse_error != OK && observation.diagnostics.is_empty()) {
		observation.diagnostics.push_back(vformat("Parser failed without diagnostics (error %d).", parse_error));
	} else if (parse_error == OK && analyzer_error != OK && observation.diagnostics.is_empty()) {
		observation.diagnostics.push_back(vformat("Analyzer failed without diagnostics (error %d).", analyzer_error));
	}
	observation.dimensions["analysis"] = parse_error == OK && analyzer_error == OK ? "accept" : "reject";
	return observation;
}

FSCompletenessObservation FSUnionCompletenessAdapter::inspect_runtime_contract(
		const FSCompletenessProgram &p_program, const Dictionary &p_runtime_context) {
	FSCompletenessObservation observation = analyze(p_program, p_program.surface);
	if (observation.dimensions.get("analysis", String()) != "accept" || !observation.diagnostics.is_empty()) {
		return observation;
	}

	String destination;
	String source_proof;
	String boundary;
	String surface;
	Dictionary coordinates;
	coordinates["destination"] = p_runtime_context.get("destination", Variant());
	coordinates["source_proof"] = p_runtime_context.get("source_proof", Variant());
	coordinates["boundary"] = p_runtime_context.get("boundary", Variant());
	coordinates["surface"] = p_runtime_context.get("surface", Variant());
	if (coordinates.size() != 4 || !read_coordinate(coordinates, "destination", destination) ||
			!read_coordinate(coordinates, "source_proof", source_proof) ||
			!read_coordinate(coordinates, "boundary", boundary) ||
			!read_coordinate(coordinates, "surface", surface) || surface != p_program.surface) {
		observation.diagnostics.push_back("Runtime contract coordinates are invalid.");
		return observation;
	}

	const Variant produced_value = p_runtime_context.get("produced_output", Variant());
	if (produced_value.get_type() != Variant::STRING) {
		observation.diagnostics.push_back("Runtime contract produced output is unavailable.");
		return observation;
	}
	observation.produced_output = produced_value;

	Ref<FoundryScript> original;
	Ref<FoundryScript> inspected;
	const Error compile_error =
			compile_runtime_contract_script(p_program, original, inspected, observation.diagnostics);
	if (compile_error != OK) {
		return observation;
	}
	if (surface == "bytecode") {
		observation.dimensions["descriptor_surface"] = "serialized_reload";
	}

	const FSDataType *descriptor =
			find_runtime_destination_descriptor(inspected, boundary, observation.diagnostics);
	if (descriptor == nullptr) {
		return observation;
	}
	if (!descriptor_is_expected_destination(*descriptor, destination)) {
		observation.diagnostics.push_back(vformat(
				"Runtime destination descriptor does not match declared '%s' coordinates.", destination));
	}
	Ref<RefCounted> unrelated;
	unrelated.instantiate();
	const Variant unrelated_value = unrelated;
	if (descriptor->is_type(unrelated_value)) {
		observation.diagnostics.push_back("Runtime destination descriptor admits RefCounted.new().");
	}
	if (!observation.diagnostics.is_empty()) {
		return observation;
	}

	const String produced_output = observation.produced_output;
	const PackedStringArray output_tokens = produced_output.strip_edges().split(" ", false);
	if (output_tokens.size() != 2 || output_tokens[0] != "uint" || output_tokens[1] != "5" ||
			produced_output != "uint 5\n") {
		observation.diagnostics.push_back(
				vformat("Runtime output does not expose the expected uint carrier: '%s'.", produced_output));
		return observation;
	}

	if (source_proof == "gradual" || source_proof == "erased" || source_proof == "variant") {
		observation.dimensions["runtime_obligation"] =
				destination == "union" ? "union_membership_check" : "typed_destination_check";
	}
	if (source_proof == "numeric_constant") {
		observation.dimensions["stored_carrier"] =
				destination == "union" ? "admitting_alternative" : "plain_destination";
	}
	return observation;
}

Error FSUnionCompletenessAdapter::execute(const String &p_scratch_root,
		const Vector<FSCompletenessProgram> &p_programs, FSCompletenessRuntimeBatch &r_batch) {
	r_batch = FSCompletenessRuntimeBatch();
	const String test_scratch_root = TemporaryProjectTree::get_test_scratch_root();
	if (test_scratch_root.is_empty()) {
		return ERR_CANT_RESOLVE;
	}
	String canonical_root;
	Error error = TemporaryProjectTree::resolve_existing_owned_path(p_scratch_root, canonical_root);
	if (error != OK) {
		return error;
	}
	error = reject_symlink_aliases(test_scratch_root, p_scratch_root);
	if (error != OK) {
		return error;
	}
	Ref<DirAccess> filesystem = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	if (filesystem.is_null() || !filesystem->dir_exists(canonical_root)) {
		return ERR_CANT_OPEN;
	}

	struct SemanticPair {
		String text_id;
		String bytecode_id;
	};
	HashMap<String, SemanticPair> pairs;
	HashSet<String> case_ids;
	HashMap<String, String> case_id_pair_keys;
	HashMap<String, String> case_id_surfaces;
	HashMap<String, FSCompletenessProgram> text_programs;
	HashMap<String, FSCompletenessProgram> bytecode_programs;
	for (const FSCompletenessProgram &program : p_programs) {
		if (!is_safe_case_id(program.case_id) || program.source.is_empty()) {
			return ERR_INVALID_DATA;
		}

		String destination;
		String source_proof;
		String boundary;
		String surface;
		if (!read_program_coordinates(program, destination, source_proof, boundary, surface) ||
				program.surface != surface) {
			return ERR_INVALID_DATA;
		}

		const String pair_key = semantic_pair_key(program.coordinates);
		if (case_ids.has(program.case_id)) {
			return case_id_pair_keys[program.case_id] == pair_key &&
							case_id_surfaces[program.case_id] == surface
					? ERR_ALREADY_IN_USE
					: ERR_ALREADY_EXISTS;
		}
		case_ids.insert(program.case_id);
		case_id_pair_keys[program.case_id] = pair_key;
		case_id_surfaces[program.case_id] = surface;
		if (program.case_id != FSCompletenessCaseID::make(union_completeness_family, program.coordinates)) {
			return ERR_INVALID_DATA;
		}

		SemanticPair *pair = pairs.getptr(pair_key);
		if (pair == nullptr) {
			pairs[pair_key] = SemanticPair();
			pair = pairs.getptr(pair_key);
		}
		if (surface == "text") {
			if (!pair->text_id.is_empty()) {
				return ERR_ALREADY_IN_USE;
			}
			pair->text_id = program.case_id;
			text_programs[program.case_id] = program;
		} else {
			if (!pair->bytecode_id.is_empty()) {
				return ERR_ALREADY_IN_USE;
			}
			pair->bytecode_id = program.case_id;
			bytecode_programs[program.case_id] = program;
		}
	}
	if (p_programs.size() != 40 || pairs.size() != 20 || text_programs.size() != 20 ||
			bytecode_programs.size() != 20) {
		return ERR_INVALID_DATA;
	}
	for (const KeyValue<String, SemanticPair> &entry : pairs) {
		if (entry.value.text_id.is_empty() || entry.value.bytecode_id.is_empty()) {
			return ERR_INVALID_DATA;
		}
	}

	const String text_root = canonical_root.path_join("text");
	const String bytecode_root = canonical_root.path_join("bytecode");
	Vector<RuntimeDestination> destinations;
	destinations.push_back({ text_root, RUNTIME_DESTINATION_DIRECTORY });
	destinations.push_back({ bytecode_root, RUNTIME_DESTINATION_DIRECTORY });
	destinations.push_back({ text_root.path_join("project.foundry"), RUNTIME_DESTINATION_FILE });
	destinations.push_back({ bytecode_root.path_join("project.foundry"), RUNTIME_DESTINATION_FILE });
	for (const FSCompletenessProgram &program : p_programs) {
		const String surface_root = program.surface == "text" ? text_root : bytecode_root;
		destinations.push_back({ surface_root.path_join(program.case_id + ".fs"), RUNTIME_DESTINATION_FILE });
		destinations.push_back({ surface_root.path_join(program.case_id + ".out"), RUNTIME_DESTINATION_FILE });
	}
	for (const RuntimeDestination &destination : destinations) {
		error = preflight_runtime_destination(canonical_root, destination);
		if (error != OK) {
			return error;
		}
	}

	error = write_runtime_file(text_root.path_join("project.foundry"), String());
	if (error != OK) {
		return error;
	}
	error = write_runtime_file(bytecode_root.path_join("project.foundry"), String());
	if (error != OK) {
		return error;
	}
	for (const FSCompletenessProgram &program : p_programs) {
		const String surface_root = program.surface == "text" ? text_root : bytecode_root;
		error = write_runtime_file(surface_root.path_join(program.case_id + ".fs"), program.source);
		if (error == OK) {
			error = write_runtime_file(
					surface_root.path_join(program.case_id + ".out"), "FS_TEST_OK\n" + program.expected_output);
		}
		if (error != OK) {
			return error;
		}
	}

	FSCompletenessRuntimeBatch completed;
	auto execute_surface = [&](const String &p_surface, const String &p_root,
								   const HashMap<String, FSCompletenessProgram> &p_surface_programs,
								   HashMap<String, FSCompletenessRuntimeResult> &r_results) -> Error {
		const bool compiled_bytecode = p_surface == "bytecode";
		FSTestRunner runner(p_root, true, false, false, compiled_bytecode);
		bool setup_ok = false;
		const Vector<FSTestRunner::FixtureOutcome> outcomes = runner.run_tests_collecting(setup_ok);
		if (!setup_ok) {
			return ERR_CANT_OPEN;
		}
		if (outcomes.size() != p_surface_programs.size()) {
			return ERR_INVALID_DATA;
		}
		HashSet<String> observed_ids;
		for (const FSTestRunner::FixtureOutcome &outcome : outcomes) {
			if (outcome.pass != p_surface || outcome.path.get_extension().to_lower() != "fs") {
				return ERR_INVALID_DATA;
			}
			const String case_id = outcome.path.get_file().get_basename();
			const FSCompletenessProgram *program = p_surface_programs.getptr(case_id);
			if (program == nullptr || observed_ids.has(case_id)) {
				return ERR_INVALID_DATA;
			}
			observed_ids.insert(case_id);
			if (outcome.expected != "FS_TEST_OK\n" + program->expected_output) {
				return ERR_INVALID_DATA;
			}

			String produced_output;
			if (extract_program_output(outcome, produced_output) != OK) {
				return ERR_INVALID_DATA;
			}
			Dictionary runtime_context = program->coordinates.duplicate();
			runtime_context["produced_output"] = produced_output;
			const FSCompletenessObservation observation = inspect_runtime_contract(*program, runtime_context);
			if (!observation.diagnostics.is_empty()) {
				return ERR_INVALID_DATA;
			}
			FSCompletenessRuntimeResult result;
			static_cast<FSCompletenessObservation &>(result) = observation;
			result.passed = outcome.passed;
			result.status = outcome.status;
			result.produced_output = produced_output;
			r_results[case_id] = result;
		}
		return observed_ids.size() == p_surface_programs.size() ? OK : ERR_INVALID_DATA;
	};

	error = execute_surface("text", text_root, text_programs, completed.text);
	if (error != OK) {
		return error;
	}
	error = execute_surface("bytecode", bytecode_root, bytecode_programs, completed.bytecode);
	if (error != OK) {
		return error;
	}

	for (const KeyValue<String, SemanticPair> &entry : pairs) {
		const FSCompletenessRuntimeResult *text = completed.text.getptr(entry.value.text_id);
		const FSCompletenessRuntimeResult *bytecode = completed.bytecode.getptr(entry.value.bytecode_id);
		if (text == nullptr || bytecode == nullptr) {
			return ERR_INVALID_DATA;
		}
		if (!observations_match_on_common_dimensions(*text, *bytecode)) {
			completed.parity_failures++;
		}
	}
	r_batch = completed;
	return OK;
}

Error FSUnionCompletenessAdapter::witness_coordinates(const String &p_witness_id, Dictionary &r_coordinates) {
	r_coordinates.clear();
	if (p_witness_id == "text_gradual_argument_binding") {
		r_coordinates["destination"] = "union";
		r_coordinates["source_proof"] = "gradual";
		r_coordinates["boundary"] = "argument_binding";
		r_coordinates["surface"] = "text";
		return OK;
	}
	if (p_witness_id == "bytecode_erased_reflective_write") {
		r_coordinates["destination"] = "union";
		r_coordinates["source_proof"] = "erased";
		r_coordinates["boundary"] = "reflective_write";
		r_coordinates["surface"] = "bytecode";
		return OK;
	}
	if (p_witness_id == "text_static_member_argument_binding") {
		r_coordinates["destination"] = "union";
		r_coordinates["source_proof"] = "static_member";
		r_coordinates["boundary"] = "argument_binding";
		r_coordinates["surface"] = "text";
		return OK;
	}
	return ERR_DOES_NOT_EXIST;
}

} // namespace FSTests
