/**************************************************************************/
/*  fs_type_completeness_union_adapter.cpp                                */
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
#include "../fs_tokenizer.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/object/ref_counted.h"
#include "core/os/mutex.h"
#include "core/os/os.h"
#include "core/templates/safe_refcount.h"

namespace FSTests {

namespace {

static const String union_completeness_family = "union_destination_membership";
static thread_local UnionCompletenessInternal::PersistedWriteTestHook persisted_write_test_hook = nullptr;

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

static const char *DIAGNOSTIC_SEVERITY_ERROR = "error";
static const char *DIAGNOSTIC_SEVERITY_WARNING = "warning";

static Dictionary make_diagnostic_record(const String &p_severity, const String &p_category,
		const String &p_code, int p_line, int p_column, const String &p_message, bool p_suppressed) {
	Dictionary record;
	record["severity"] = p_severity;
	record["category"] = p_category;
	record["code"] = p_code;
	record["line"] = double(p_line);
	record["column"] = double(p_column);
	record["message"] = p_message;
	record["suppressed"] = p_suppressed;
	return record;
}

static void append_error_diagnostic(FSCompletenessObservation &r_observation, const String &p_code,
		const String &p_message) {
	r_observation.diagnostics.push_back(p_message);
	r_observation.diagnostic_records.push_back(make_diagnostic_record(
			DIAGNOSTIC_SEVERITY_ERROR, "harness", p_code, 0, 0, p_message, false));
}

// Every diagnostic string must own an error record so the severity profile derived from
// `diagnostic_records` can never understate what the observation already reported.
static void cover_diagnostics_with_records(FSCompletenessObservation &r_observation) {
	HashMap<String, int> recorded_messages;
	for (int index = 0; index < r_observation.diagnostic_records.size(); index++) {
		const Dictionary record = r_observation.diagnostic_records[index];
		if (String(record.get("severity", String())) != DIAGNOSTIC_SEVERITY_ERROR) {
			continue;
		}
		const String message = record.get("message", String());
		recorded_messages[message] = recorded_messages.has(message) ? recorded_messages[message] + 1 : 1;
	}
	for (const String &diagnostic : r_observation.diagnostics) {
		int *remaining = recorded_messages.getptr(diagnostic);
		if (remaining != nullptr && *remaining > 0) {
			(*remaining)--;
			continue;
		}
		r_observation.diagnostic_records.push_back(make_diagnostic_record(
				DIAGNOSTIC_SEVERITY_ERROR, "harness", "harness_diagnostic", 0, 0, diagnostic, false));
	}
}

static void append_parser_diagnostics_in_source_order(
		const FSParser &p_parser, PackedStringArray &r_diagnostics) {
	for (const FSParser::ParserError *error : p_parser.get_errors_in_source_order()) {
		if (error != nullptr) {
			r_diagnostics.push_back(vformat("%d:%d: %s", error->line, error->column, error->message));
		}
	}
}

static void append_parser_diagnostics_in_source_order(const FSParser &p_parser, const String &p_code,
		FSCompletenessObservation &r_observation) {
	for (const FSParser::ParserError *error : p_parser.get_errors_in_source_order()) {
		if (error != nullptr) {
			const String message = vformat("%d:%d: %s", error->line, error->column, error->message);
			r_observation.diagnostics.push_back(message);
			r_observation.diagnostic_records.push_back(make_diagnostic_record(DIAGNOSTIC_SEVERITY_ERROR,
					"analysis", p_code, error->line, error->column, message, false));
		}
	}
}

// Offset of the first character of every line, so a token's (line, column) can be turned into an
// index into the source.
static Vector<int> line_start_offsets(const String &p_source) {
	Vector<int> offsets;
	offsets.push_back(0);
	for (int index = 0; index < p_source.length(); index++) {
		if (p_source[index] == U'\n') {
			offsets.push_back(index + 1);
		}
	}
	return offsets;
}

// The tokenizer reports display columns: a tab advances the column by its configured tab size, which
// an editor setting can change. Asking the tokenizer what it does to a known tab keeps the mapping
// from column back to character index correct without duplicating that setting here.
static int tokenizer_tab_size() {
	FSTokenizerText tokenizer;
	tokenizer.set_source_code("func calibrate():\n\t@calibrate\n");
	for (FSTokenizer::Token token = tokenizer.scan();
			token.type != FSTokenizer::Token::TK_EOF && token.type != FSTokenizer::Token::ERROR;
			token = tokenizer.scan()) {
		if (token.type == FSTokenizer::Token::ANNOTATION) {
			return MAX(1, token.start_column - 1);
		}
	}
	return 4;
}

static int offset_for_column(
		const String &p_source, int p_line_begin, int p_target_column, int p_tab_size) {
	int column = 1;
	int index = p_line_begin;
	while (index < p_source.length() && p_source[index] != U'\n') {
		if (column >= p_target_column) {
			return index;
		}
		column += p_source[index] == U'\t' ? p_tab_size : 1;
		index++;
	}
	return index;
}

// The display column of the character at `p_offset`, counted the way the tokenizer counts it.
static int display_column_at(
		const String &p_source, int p_line_begin, int p_offset, int p_tab_size) {
	int column = 1;
	for (int index = p_line_begin; index < p_offset && index < p_source.length(); index++) {
		if (p_source[index] == U'\n') {
			break;
		}
		column += p_source[index] == U'\t' ? p_tab_size : 1;
	}
	return column;
}

static int display_width(const String &p_text, int p_tab_size) {
	int width = 0;
	for (int index = 0; index < p_text.length(); index++) {
		width += p_text[index] == U'\t' ? p_tab_size : 1;
	}
	return width;
}

static int token_offset(const String &p_source, const Vector<int> &p_line_offsets, int p_line,
		int p_column, int p_tab_size) {
	const int line_index = p_line - 1;
	if (line_index < 0 || line_index >= p_line_offsets.size() || p_column < 1) {
		return -1;
	}
	return offset_for_column(p_source, p_line_offsets[line_index], p_column, p_tab_size);
}

struct WarningIgnoreAnnotationSpan {
	int start = 0;
	int end = 0;
	int start_line = 0;
};

// Locates every warning-suppression annotation through the front-end's own lexer, so string
// literals, comments, and multiline argument lists are recognized the way the language defines them
// rather than by re-deriving them here, and an annotation is found wherever it may legally appear.
static Vector<WarningIgnoreAnnotationSpan> find_warning_ignore_annotations(
		const String &p_source, const Vector<int> &p_line_offsets, int p_tab_size) {
	Vector<WarningIgnoreAnnotationSpan> spans;
	const int tab_size = p_tab_size;
	FSTokenizerText tokenizer;
	tokenizer.set_source_code(p_source);
	FSTokenizer::Token token = tokenizer.scan();
	while (token.type != FSTokenizer::Token::TK_EOF && token.type != FSTokenizer::Token::ERROR) {
		if (token.type != FSTokenizer::Token::ANNOTATION || !token.source.begins_with("@warning_ignore")) {
			token = tokenizer.scan();
			continue;
		}
		WarningIgnoreAnnotationSpan span;
		span.start = token_offset(p_source, p_line_offsets, token.start_line, token.start_column, tab_size);
		span.end = token_offset(p_source, p_line_offsets, token.end_line, token.end_column, tab_size);
		span.start_line = token.start_line;

		FSTokenizer::Token next = tokenizer.scan();
		if (next.type == FSTokenizer::Token::PARENTHESIS_OPEN) {
			int depth = 1;
			while (depth > 0) {
				next = tokenizer.scan();
				if (next.type == FSTokenizer::Token::TK_EOF || next.type == FSTokenizer::Token::ERROR) {
					break;
				}
				if (next.type == FSTokenizer::Token::PARENTHESIS_OPEN) {
					depth++;
				} else if (next.type == FSTokenizer::Token::PARENTHESIS_CLOSE) {
					depth--;
					if (depth == 0) {
						span.end = token_offset(
								p_source, p_line_offsets, next.end_line, next.end_column, tab_size);
					}
				}
			}
			next = tokenizer.scan();
		}
		if (span.start >= 0 && span.end > span.start) {
			spans.push_back(span);
		}
		token = next;
	}
	return spans;
}

// Enumerates the diagnostics the analyzer would raise once annotation suppression is removed. A
// warning promoted to error level never reaches the warning list, so errors are collected too:
// otherwise a diagnostic that a `@warning_ignore` hides would leave no severity evidence at all.
static void append_unsuppressed_diagnostic_records(
		const String &p_source, const String &p_path, FSCompletenessObservation &r_observation) {
#ifdef DEBUG_ENABLED
	struct ObservedDiagnostic {
		String severity;
		String code;
		int line = 0;
		int column = 0;
		String message;
	};
	auto collect = [](const String &p_analyzed_source, const String &p_analyzed_path,
						   Vector<ObservedDiagnostic> &r_diagnostics) {
		FSParser parser;
		if (parser.parse(p_analyzed_source, p_analyzed_path, false) == OK) {
			FSAnalyzer analyzer(&parser);
			analyzer.analyze();
		}
		for (const FSParser::ParserError *error : parser.get_errors_in_source_order()) {
			if (error == nullptr) {
				continue;
			}
			ObservedDiagnostic observed;
			observed.severity = DIAGNOSTIC_SEVERITY_ERROR;
			observed.code = "suppressed_analysis_error";
			observed.line = error->line;
			observed.column = error->column;
			observed.message = error->message;
			r_diagnostics.push_back(observed);
		}
		for (const FSWarning &warning : parser.get_warnings()) {
			ObservedDiagnostic observed;
			observed.severity = DIAGNOSTIC_SEVERITY_WARNING;
			observed.code = FSWarning::get_name_from_code(warning.code);
			observed.line = warning.start_line;
			observed.column = warning.start_column;
			observed.message = warning.get_message();
			r_diagnostics.push_back(observed);
		}
	};
	// Consumes the first unclaimed diagnostic identical to `p_diagnostic`. Matching is a multiset
	// operation, not membership: two distinct diagnostics can share a position, and every error
	// carries the same code, so a claimed entry must not answer for a second one. The message is part
	// of the key because it is the only field that separates same-code diagnostics at one position.
	auto claim_matching = [](Vector<ObservedDiagnostic> &r_diagnostics, Vector<bool> &r_claimed,
								  const ObservedDiagnostic &p_diagnostic) {
		for (int index = 0; index < r_diagnostics.size(); index++) {
			if (r_claimed[index]) {
				continue;
			}
			const ObservedDiagnostic &candidate = r_diagnostics[index];
			if (candidate.severity == p_diagnostic.severity && candidate.code == p_diagnostic.code &&
					candidate.line == p_diagnostic.line && candidate.column == p_diagnostic.column &&
					candidate.message == p_diagnostic.message) {
				r_claimed.write[index] = true;
				return true;
			}
		}
		return false;
	};

	Vector<ObservedDiagnostic> emitted;
	collect(p_source, p_path, emitted);
	Vector<ObservedDiagnostic> unsuppressed;
	const FSCompletenessProbeSource probe = make_unsuppressed_probe_source(p_source);
	if (probe.text == p_source) {
		unsuppressed = emitted;
	} else {
		collect(probe.text, p_path, unsuppressed);
	}
	Vector<bool> claimed;
	claimed.resize(emitted.size());
	claimed.fill(false);

	for (const ObservedDiagnostic &diagnostic : unsuppressed) {
		// Removing an inline annotation shifts everything after it on that line, so the probe's
		// coordinates are translated back to the original source before anything is matched or
		// recorded. Without that, a diagnostic the primary analysis still reported would look new.
		ObservedDiagnostic original = diagnostic;
		original.column = probe.original_column(diagnostic.line, diagnostic.column);
		const bool suppressed = !claim_matching(emitted, claimed, original);
		// An error the primary analysis already reported owns a record from the primary pass.
		if (original.severity == DIAGNOSTIC_SEVERITY_ERROR && !suppressed) {
			continue;
		}
		const String message = original.severity == DIAGNOSTIC_SEVERITY_ERROR
				? vformat("%d:%d: %s", original.line, original.column, original.message)
				: original.message;
		r_observation.diagnostic_records.push_back(make_diagnostic_record(original.severity, "analysis",
				original.code, original.line, original.column, message, suppressed));
	}
#else
	(void)p_source;
	(void)p_path;
	(void)r_observation;
#endif // DEBUG_ENABLED
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
	append_error_diagnostic(observation, "harness_rejected_program", p_diagnostic);
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

class RuntimeInvocationScope {
	String path;

public:
	~RuntimeInvocationScope() {
		if (path.is_empty()) {
			return;
		}
		const Error cleanup_error = TemporaryProjectTree::remove_owned_path(path);
		if (cleanup_error != OK) {
			ERR_PRINT(vformat("Could not remove completeness runtime staging '%s' (error %d).", path, cleanup_error));
		}
	}

	Error create(const String &p_parent) {
		static SafeNumeric<uint64_t> invocation_sequence;
		for (int attempt = 0; attempt < 4096; attempt++) {
			const String candidate = p_parent.path_join(vformat("fstc-runtime-%d-%s",
					OS::get_singleton()->get_process_id(),
					String::num_uint64(invocation_sequence.increment())));
			const Error create_error = DirAccess::make_dir_absolute(candidate);
			if (create_error == ERR_ALREADY_EXISTS) {
				continue;
			}
			if (create_error != OK) {
				return create_error;
			}
			path = candidate;
			String canonical_candidate;
			const Error resolve_error = TemporaryProjectTree::resolve_existing_owned_path(
					candidate, canonical_candidate);
			if (resolve_error != OK || canonical_candidate != candidate.simplify_path()) {
				return resolve_error == OK ? ERR_UNAUTHORIZED : resolve_error;
			}
			return OK;
		}
		return ERR_ALREADY_EXISTS;
	}

	const String &get_path() const { return path; }
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

static Error write_runtime_file_exclusive(const String &p_path, const String &p_contents) {
	Error file_error = OK;
	Ref<FileAccess> file = FileAccess::open(
			p_path, FileAccess::WRITE | FileAccess::WRITE_EXCL, &file_error);
	if (file.is_null()) {
		return file_error == OK ? ERR_CANT_CREATE : file_error;
	}
	if (!file->store_string(p_contents)) {
		file_error = file->get_error();
		file->close();
		file.unref();
		return file_error == OK ? ERR_CANT_CREATE : file_error;
	}
	file->flush();
	file_error = file->get_error();
	file->close();
	file.unref();
	if (file_error != OK) {
		return file_error;
	}
	if (persisted_write_test_hook != nullptr) {
		persisted_write_test_hook(p_path);
	}
	Error read_error = OK;
	const String persisted = FileAccess::get_file_as_string(p_path, &read_error);
	if (read_error != OK) {
		return read_error;
	}
	return persisted == p_contents ? OK : ERR_FILE_CORRUPT;
}

static Error validate_runtime_staging_whitelist(
		const String &p_invocation_root, const HashSet<String> &p_expected_files) {
	Ref<DirAccess> root = DirAccess::open(p_invocation_root);
	if (root.is_null()) {
		return ERR_CANT_OPEN;
	}
	root->set_include_hidden(true);
	HashSet<String> observed_surfaces;
	root->list_dir_begin();
	for (String entry = root->get_next(); !entry.is_empty(); entry = root->get_next()) {
		if (entry == "." || entry == "..") {
			continue;
		}
		const String child = p_invocation_root.path_join(entry);
		if (root->is_link(child)) {
			root->list_dir_end();
			return ERR_UNAUTHORIZED;
		}
		if (!root->current_is_dir() || (entry != "text" && entry != "bytecode") ||
				observed_surfaces.has(entry)) {
			root->list_dir_end();
			return ERR_INVALID_DATA;
		}
		observed_surfaces.insert(entry);
	}
	root->list_dir_end();
	if (observed_surfaces.size() != 2) {
		return ERR_INVALID_DATA;
	}

	HashSet<String> observed_files;
	for (const String &surface : { String("text"), String("bytecode") }) {
		const String surface_root = p_invocation_root.path_join(surface);
		Ref<DirAccess> directory = DirAccess::open(surface_root);
		if (directory.is_null()) {
			return ERR_CANT_OPEN;
		}
		directory->set_include_hidden(true);
		directory->list_dir_begin();
		for (String entry = directory->get_next(); !entry.is_empty(); entry = directory->get_next()) {
			if (entry == "." || entry == "..") {
				continue;
			}
			const String child = surface_root.path_join(entry);
			const String relative_path = surface.path_join(entry);
			if (directory->is_link(child)) {
				directory->list_dir_end();
				return ERR_UNAUTHORIZED;
			}
			if (directory->current_is_dir() || !p_expected_files.has(relative_path) ||
					observed_files.has(relative_path)) {
				directory->list_dir_end();
				return ERR_INVALID_DATA;
			}
			observed_files.insert(relative_path);
		}
		directory->list_dir_end();
	}
	return observed_files.size() == p_expected_files.size() ? OK : ERR_INVALID_DATA;
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

enum RuntimeInspectionDisposition {
	RUNTIME_INSPECTION_COMPLETE,
	RUNTIME_INSPECTION_PRODUCT_FAILURE,
	RUNTIME_INSPECTION_STRUCTURAL_FAILURE,
};

struct RuntimeInspectionStepResult {
	RuntimeInspectionDisposition disposition = RUNTIME_INSPECTION_COMPLETE;
	Error error = OK;
};

static bool is_runtime_inspection_resource_failure(const Error p_error) {
	switch (p_error) {
		case ERR_OUT_OF_MEMORY:
		case ERR_FILE_NOT_FOUND:
		case ERR_FILE_BAD_DRIVE:
		case ERR_FILE_BAD_PATH:
		case ERR_FILE_NO_PERMISSION:
		case ERR_FILE_ALREADY_IN_USE:
		case ERR_FILE_CANT_OPEN:
		case ERR_FILE_CANT_WRITE:
		case ERR_FILE_CANT_READ:
		case ERR_FILE_EOF:
		case ERR_CANT_OPEN:
		case ERR_CANT_CREATE:
		case ERR_CANT_ACQUIRE_RESOURCE:
			return true;
		default:
			return false;
	}
}

static RuntimeInspectionStepResult runtime_inspection_failure(
		const Error p_error, const RuntimeInspectionDisposition p_disposition) {
	RuntimeInspectionStepResult result;
	result.disposition = p_disposition == RUNTIME_INSPECTION_STRUCTURAL_FAILURE ||
					is_runtime_inspection_resource_failure(p_error)
			? RUNTIME_INSPECTION_STRUCTURAL_FAILURE
			: RUNTIME_INSPECTION_PRODUCT_FAILURE;
	result.error = p_error == OK ? ERR_INVALID_DATA : p_error;
	return result;
}

static RuntimeInspectionStepResult compile_runtime_contract_script(const FSCompletenessProgram &p_program,
		Ref<FoundryScript> &r_original, Ref<FoundryScript> &r_inspected, PackedStringArray &r_diagnostics) {
	r_original.unref();
	r_inspected.unref();
	UnionCompletenessInternal::SyntheticSourceScope synthetic_source(
			"runtime_contract_" + p_program.case_id, p_program.source);
	if (!synthetic_source.is_available()) {
		r_diagnostics.push_back("Runtime contract source identity is unavailable.");
		return runtime_inspection_failure(ERR_CANT_CREATE, RUNTIME_INSPECTION_STRUCTURAL_FAILURE);
	}

	Error error = OK;
	r_original = FSCache::get_shallow_script(synthetic_source.get_path(), error);
	if (error != OK || r_original.is_null()) {
		r_diagnostics.push_back(vformat("Runtime contract script setup failed (error %d).", error));
		return runtime_inspection_failure(
				error == OK ? ERR_CANT_CREATE : error, RUNTIME_INSPECTION_STRUCTURAL_FAILURE);
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
		return runtime_inspection_failure(error, RUNTIME_INSPECTION_PRODUCT_FAILURE);
	}

	r_inspected = r_original;
	if (p_program.surface == "bytecode") {
#ifdef TOOLS_ENABLED
		FSBytecodeExporter exporter;
		Vector<uint8_t> buffer;
		error = exporter.serialize(r_original, buffer);
		if (error != OK) {
			r_diagnostics.push_back(vformat("Runtime contract serialization failed (error %d).", error));
			return runtime_inspection_failure(error, RUNTIME_INSPECTION_PRODUCT_FAILURE);
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
		if (restored.is_null() || restored.ptr() == r_original.ptr()) {
			r_diagnostics.push_back(vformat("Runtime contract bytecode reload failed (error %d).", error));
			r_inspected.unref();
			return runtime_inspection_failure(
					error == OK ? ERR_INVALID_DATA : error, RUNTIME_INSPECTION_STRUCTURAL_FAILURE);
		}
		if (error != OK || !restored->is_valid() || !restored->is_compiled_binary()) {
			r_diagnostics.push_back(vformat("Runtime contract bytecode reload failed (error %d).", error));
			r_inspected.unref();
			return runtime_inspection_failure(error, RUNTIME_INSPECTION_PRODUCT_FAILURE);
		}
		r_inspected = restored;
#else
		r_diagnostics.push_back("Runtime contract bytecode reload is unavailable in this build.");
		return runtime_inspection_failure(ERR_UNAVAILABLE, RUNTIME_INSPECTION_STRUCTURAL_FAILURE);
#endif
	}
	return RuntimeInspectionStepResult();
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

struct RuntimeDescriptorEvidence {
	FSDataType descriptor;
	ObjectID inspected_instance_id;
	ObjectID inspected_owner_instance_id;
	bool inspected_compiled_binary = false;
};

static RuntimeInspectionStepResult inspect_runtime_destination_descriptor(const Ref<FoundryScript> &p_inspected,
		const String &p_boundary, RuntimeDescriptorEvidence &r_evidence,
		PackedStringArray &r_diagnostics) {
	r_evidence = RuntimeDescriptorEvidence();
	if (p_inspected.is_null()) {
		r_diagnostics.push_back("Compiled runtime script is unavailable for descriptor inspection.");
		return runtime_inspection_failure(ERR_INVALID_DATA, RUNTIME_INSPECTION_STRUCTURAL_FAILURE);
	}
	r_evidence.inspected_instance_id = p_inspected->get_instance_id();
	r_evidence.inspected_compiled_binary = p_inspected->is_compiled_binary();
	if (p_boundary == "argument_binding") {
		FSFunction *const *accept = p_inspected->get_member_functions().getptr(SNAME("accept"));
		if (accept == nullptr || *accept == nullptr || (*accept)->get_argument_count() != 1) {
			r_diagnostics.push_back("Compiled accept function does not expose one destination argument.");
			return runtime_inspection_failure(ERR_INVALID_DATA, RUNTIME_INSPECTION_PRODUCT_FAILURE);
		}
		r_evidence.descriptor = (*accept)->get_argument_type(0);
		return RuntimeInspectionStepResult();
	}

	const Ref<FoundryScript> *holder = p_inspected->get_subclasses().getptr(SNAME("Holder"));
	if (holder == nullptr || holder->is_null()) {
		r_diagnostics.push_back("Compiled Holder class is unavailable.");
		return runtime_inspection_failure(ERR_INVALID_DATA, RUNTIME_INSPECTION_PRODUCT_FAILURE);
	}
	r_evidence.inspected_owner_instance_id = (*holder)->get_instance_id();
	const FSDataType *descriptor = (*holder)->find_member_data_type(SNAME("value"));
	if (descriptor == nullptr) {
		r_diagnostics.push_back("Compiled Holder.value descriptor is unavailable.");
		return runtime_inspection_failure(ERR_INVALID_DATA, RUNTIME_INSPECTION_PRODUCT_FAILURE);
	}
	r_evidence.descriptor = *descriptor;
	return RuntimeInspectionStepResult();
}

static bool is_surface_identity_dimension(const Variant &p_key) {
	return p_key == "original_instance_id" || p_key == "inspected_instance_id" ||
			p_key == "inspected_compiled_binary" || p_key == "inspected_owner_instance_id";
}

static bool observations_match_on_common_dimensions(
		const FSCompletenessRuntimeResult &p_text, const FSCompletenessRuntimeResult &p_bytecode) {
	if (compare_surface_evidence(p_text, p_bytecode).any()) {
		return false;
	}
	for (const Variant &key : p_text.dimensions.keys()) {
		if (is_surface_identity_dimension(key)) {
			continue;
		}
		if (p_bytecode.dimensions.has(key) && p_text.dimensions[key] != p_bytecode.dimensions[key]) {
			return false;
		}
	}
	return true;
}

static String extract_program_output(const FSTestRunner::FixtureOutcome &p_outcome) {
	const String status_line = "FS_TEST_OK\n";
	if (p_outcome.output.begins_with(status_line)) {
		return p_outcome.output.trim_prefix(status_line);
	}
	return p_outcome.output;
}

} // namespace

void UnionCompletenessInternal::set_persisted_write_test_hook(PersistedWriteTestHook p_hook) {
	persisted_write_test_hook = p_hook;
}

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
		append_error_diagnostic(
				observation, "harness_identity_unavailable", "In-memory analyzer identity is unavailable.");
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
	append_parser_diagnostics_in_source_order(
			parser, parse_error == OK ? "analyzer_error" : "parse_error", observation);
	if (parse_error != OK && observation.diagnostics.is_empty()) {
		append_error_diagnostic(observation, "parse_error",
				vformat("Parser failed without diagnostics (error %d).", parse_error));
	} else if (parse_error == OK && analyzer_error != OK && observation.diagnostics.is_empty()) {
		append_error_diagnostic(observation, "analyzer_error",
				vformat("Analyzer failed without diagnostics (error %d).", analyzer_error));
	}
	append_unsuppressed_diagnostic_records(p_program.source, path, observation);
	observation.dimensions["analysis"] = parse_error == OK && analyzer_error == OK ? "accept" : "reject";
	cover_diagnostics_with_records(observation);
	return observation;
}

static FSCompletenessObservation inspect_runtime_contract_body(
		const FSCompletenessProgram &p_program, const Dictionary &p_runtime_context, Error &r_structural_error) {
	r_structural_error = OK;
	FSCompletenessObservation observation = FSUnionCompletenessAdapter::analyze(p_program, p_program.surface);

	String destination;
	String source_proof;
	String boundary;
	String surface;
	if (!read_program_coordinates(p_program, destination, source_proof, boundary, surface) ||
			surface != p_program.surface) {
		observation.diagnostics.push_back("Runtime contract coordinates are invalid.");
		r_structural_error = ERR_INVALID_DATA;
		return observation;
	}
	for (const String &axis : { String("destination"), String("source_proof"), String("boundary"), String("surface") }) {
		if (p_runtime_context.has(axis) &&
				p_runtime_context.get(axis, Variant()) != p_program.coordinates.get(axis, Variant())) {
			observation.diagnostics.push_back(
					vformat("Runtime context coordinate '%s' disagrees with the program.", axis));
			r_structural_error = ERR_INVALID_DATA;
			return observation;
		}
	}

	const Variant produced_value = p_runtime_context.get("produced_output", Variant());
	if (produced_value.get_type() != Variant::STRING) {
		observation.diagnostics.push_back("Runtime contract produced output is unavailable.");
		r_structural_error = ERR_INVALID_DATA;
		return observation;
	}
	observation.produced_output = produced_value;
	if (observation.dimensions.get("analysis", String()) != "accept" || !observation.diagnostics.is_empty()) {
		return observation;
	}

	Ref<FoundryScript> original;
	Ref<FoundryScript> inspected;
	const RuntimeInspectionStepResult compile_result =
			compile_runtime_contract_script(p_program, original, inspected, observation.diagnostics);
	if (original.is_valid()) {
		observation.dimensions["original_instance_id"] = int64_t(original->get_instance_id());
	}
	if (compile_result.disposition != RUNTIME_INSPECTION_COMPLETE) {
		if (compile_result.disposition == RUNTIME_INSPECTION_STRUCTURAL_FAILURE) {
			r_structural_error = compile_result.error;
		}
		return observation;
	}
	const ObjectID original_instance_id = original->get_instance_id();
	if (surface == "bytecode") {
		// Descriptor inspection below receives only the restored script. Dropping the original local
		// reference makes an accidental query through the source-compiled object impossible here.
		original.unref();
	}
	RuntimeDescriptorEvidence descriptor_evidence;
	const RuntimeInspectionStepResult descriptor_result = inspect_runtime_destination_descriptor(
			inspected, boundary, descriptor_evidence, observation.diagnostics);
	observation.dimensions["original_instance_id"] = int64_t(original_instance_id);
	if (descriptor_evidence.inspected_instance_id.is_valid()) {
		observation.dimensions["inspected_instance_id"] = int64_t(descriptor_evidence.inspected_instance_id);
	}
	observation.dimensions["inspected_compiled_binary"] = descriptor_evidence.inspected_compiled_binary;
	if (boundary == "reflective_write" && descriptor_evidence.inspected_owner_instance_id.is_valid()) {
		observation.dimensions["inspected_owner_instance_id"] =
				int64_t(descriptor_evidence.inspected_owner_instance_id);
	}
	if (descriptor_result.disposition != RUNTIME_INSPECTION_COMPLETE) {
		if (descriptor_result.disposition == RUNTIME_INSPECTION_STRUCTURAL_FAILURE) {
			r_structural_error = descriptor_result.error;
		}
		return observation;
	}

	if (!descriptor_is_expected_destination(descriptor_evidence.descriptor, destination)) {
		observation.diagnostics.push_back(vformat(
				"Runtime destination descriptor does not match declared '%s' coordinates.", destination));
	}
	Ref<RefCounted> unrelated;
	unrelated.instantiate();
	const Variant unrelated_value = unrelated;
	if (descriptor_evidence.descriptor.is_type(unrelated_value)) {
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

static FSCompletenessObservation inspect_runtime_contract_internal(
		const FSCompletenessProgram &p_program, const Dictionary &p_runtime_context, Error &r_structural_error) {
	FSCompletenessObservation observation =
			inspect_runtime_contract_body(p_program, p_runtime_context, r_structural_error);
	cover_diagnostics_with_records(observation);
	return observation;
}

FSCompletenessObservation FSUnionCompletenessAdapter::inspect_runtime_contract(
		const FSCompletenessProgram &p_program, const Dictionary &p_runtime_context) {
	Error structural_error = OK;
	return inspect_runtime_contract_internal(p_program, p_runtime_context, structural_error);
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

	RuntimeInvocationScope invocation;
	error = invocation.create(canonical_root);
	if (error != OK) {
		return error;
	}
	const String text_root = invocation.get_path().path_join("text");
	const String bytecode_root = invocation.get_path().path_join("bytecode");
	error = DirAccess::make_dir_absolute(text_root);
	if (error != OK) {
		return error;
	}
	error = DirAccess::make_dir_absolute(bytecode_root);
	if (error != OK) {
		return error;
	}

	Vector<RuntimeDestination> destinations;
	destinations.push_back({ text_root, RUNTIME_DESTINATION_DIRECTORY });
	destinations.push_back({ bytecode_root, RUNTIME_DESTINATION_DIRECTORY });
	destinations.push_back({ text_root.path_join("project.foundry"), RUNTIME_DESTINATION_FILE });
	destinations.push_back({ bytecode_root.path_join("project.foundry"), RUNTIME_DESTINATION_FILE });
	HashSet<String> expected_files;
	expected_files.insert("text/project.foundry");
	expected_files.insert("bytecode/project.foundry");
	for (const FSCompletenessProgram &program : p_programs) {
		const String surface_root = program.surface == "text" ? text_root : bytecode_root;
		destinations.push_back({ surface_root.path_join(program.case_id + ".fs"), RUNTIME_DESTINATION_FILE });
		destinations.push_back({ surface_root.path_join(program.case_id + ".out"), RUNTIME_DESTINATION_FILE });
		expected_files.insert(program.surface.path_join(program.case_id + ".fs"));
		expected_files.insert(program.surface.path_join(program.case_id + ".out"));
	}
	for (const RuntimeDestination &destination : destinations) {
		error = preflight_runtime_destination(invocation.get_path(), destination);
		if (error != OK) {
			return error;
		}
	}

	error = write_runtime_file_exclusive(text_root.path_join("project.foundry"), String());
	if (error != OK) {
		return error;
	}
	error = write_runtime_file_exclusive(bytecode_root.path_join("project.foundry"), String());
	if (error != OK) {
		return error;
	}
	for (const FSCompletenessProgram &program : p_programs) {
		const String surface_root = program.surface == "text" ? text_root : bytecode_root;
		error = write_runtime_file_exclusive(
				surface_root.path_join(program.case_id + ".fs"), program.source);
		if (error == OK) {
			error = write_runtime_file_exclusive(
					surface_root.path_join(program.case_id + ".out"), "FS_TEST_OK\n" + program.expected_output);
		}
		if (error != OK) {
			return error;
		}
	}
	error = validate_runtime_staging_whitelist(invocation.get_path(), expected_files);
	if (error != OK) {
		return error;
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

			const String produced_output = extract_program_output(outcome);
			Dictionary runtime_context;
			runtime_context["produced_output"] = produced_output;
			Error inspection_error = OK;
			FSCompletenessObservation observation =
					inspect_runtime_contract_internal(*program, runtime_context, inspection_error);
			if (inspection_error != OK || observation.case_id != case_id || observation.surface != p_surface) {
				return inspection_error == OK ? ERR_INVALID_DATA : inspection_error;
			}
			FSCompletenessRuntimeResult result;
			static_cast<FSCompletenessObservation &>(result) = observation;
			result.passed = outcome.passed && observation.diagnostics.is_empty();
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

// Removes exactly the characters a warning-suppression annotation owns: its name, its argument list
// however many lines that spans, and the whitespace separating it from what follows on that line.
// Whitespace that belongs to the target statement is never removed. The annotation's own indentation
// is re-established on the line its last character sits on, so a target that shares that line stays
// at the block level the annotation introduced instead of collapsing to file scope. Every removed
// newline is re-emitted, so line numbers survive, and the characters removed before the surviving
// content of a line are recorded so a column can be mapped back to the original program.
FSCompletenessProbeSource make_unsuppressed_probe_source(const String &p_source) {
	FSCompletenessProbeSource probe;
	probe.text = p_source;
	const int tab_size = tokenizer_tab_size();
	const Vector<int> line_offsets = line_start_offsets(p_source);
	const Vector<WarningIgnoreAnnotationSpan> spans =
			find_warning_ignore_annotations(p_source, line_offsets, tab_size);
	if (spans.is_empty()) {
		return probe;
	}

	String stripped;
	int copied_from = 0;
	for (const WarningIgnoreAnnotationSpan &span : spans) {
		if (span.start < copied_from) {
			continue;
		}
		// The separator belongs to the annotation, not to the target: leaving it would append spaces
		// to a tab indent and make the indentation of the probe source inconsistent.
		int span_end = span.end;
		while (span_end < p_source.length() && (p_source[span_end] == U' ' || p_source[span_end] == U'\t')) {
			span_end++;
		}

		const int line_index = span.start_line - 1;
		const int line_begin = line_index >= 0 && line_index < line_offsets.size()
				? line_offsets[line_index]
				: 0;
		int indentation_width = 0;
		while (line_begin + indentation_width < span.start &&
				(p_source[line_begin + indentation_width] == U' ' ||
						p_source[line_begin + indentation_width] == U'\t')) {
			indentation_width++;
		}
		const String statement_indentation = p_source.substr(line_begin, indentation_width);

		stripped += p_source.substr(copied_from, span.start - copied_from);
		int removed_newlines = 0;
		int final_line_start = span.start;
		for (int scan = span.start; scan < span_end; scan++) {
			if (p_source[scan] == U'\n') {
				removed_newlines++;
				final_line_start = scan + 1;
			}
		}
		for (int emitted = 0; emitted < removed_newlines; emitted++) {
			stripped += "\n";
		}
		const bool crossed_lines = removed_newlines > 0;
		if (crossed_lines) {
			stripped += statement_indentation;
		}
		// Diagnostic columns are display columns, so the shift has to be measured in the same unit: a
		// tab inside the removed span or the separator counts for the tokenizer's tab width, not one.
		const int final_line = span.start_line + removed_newlines;
		const int surviving_column_before = crossed_lines
				? display_column_at(p_source, final_line_start, span_end, tab_size)
				: display_column_at(p_source, line_begin, span_end, tab_size);
		const int surviving_column_after = crossed_lines
				? 1 + display_width(statement_indentation, tab_size)
				: display_column_at(p_source, line_begin, span.start, tab_size);
		const int *previous_shift = probe.column_shift_by_line.getptr(final_line);
		probe.column_shift_by_line[final_line] = (previous_shift == nullptr ? 0 : *previous_shift) +
				surviving_column_before - surviving_column_after;
		copied_from = span_end;
	}
	stripped += p_source.substr(copied_from);
	probe.text = stripped;
	return probe;
}

FSCompletenessSurfaceEvidenceMismatch compare_surface_evidence(
		const FSCompletenessRuntimeResult &p_text, const FSCompletenessRuntimeResult &p_bytecode) {
	FSCompletenessSurfaceEvidenceMismatch mismatch;
	mismatch.produced_output = p_text.produced_output != p_bytecode.produced_output;
	mismatch.diagnostics = p_text.diagnostics != p_bytecode.diagnostics;
	mismatch.diagnostic_records = p_text.diagnostic_records != p_bytecode.diagnostic_records;
	mismatch.runtime_status = p_text.passed != p_bytecode.passed || p_text.status != p_bytecode.status;
	return mismatch;
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
