/**************************************************************************/
/*  fs_name_mangler_export.cpp                                            */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
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

#include "fs_name_mangler_export.h"

#ifdef TOOLS_ENABLED

#include "fs_export_compilation_scope.h"

#include "../foundry_script.h"
#include "../fs_bytecode_export.h"
#include "../fs_bytecode_loader.h"
#include "../fs_cache.h"
#include "../fs_name_mangler_analysis.h"
#include "../fs_name_mangler_binding_safety.h"
#include "../fs_name_mangler_keep_rules.h"
#include "../fs_parser.h"

#include "core/config/project_settings.h"
#include "core/io/file_access.h"
#include "core/io/resource_loader.h"

namespace {

struct ExportDiagnosticComparator {
	bool operator()(const FSNameManglerExport::Diagnostic &p_left,
			const FSNameManglerExport::Diagnostic &p_right) const {
		if (p_left.stage != p_right.stage) {
			return p_left.stage < p_right.stage;
		}
		if (p_left.source != p_right.source) {
			return p_left.source < p_right.source;
		}
		return p_left.message < p_right.message;
	}
};

struct DiscoveredScript {
	String source_path;
	Ref<FoundryScript> script;
	bool annotated_static_unload = false;
	Vector<String> dependencies;
};

struct DiscoveredResource {
	String source_path;
	Ref<Resource> resource;
};

struct DiscoveredGraph {
	RBMap<String, DiscoveredScript> scripts;
	Vector<DiscoveredResource> resources;
};

String canonical_script_path(const String &p_path) {
	const String remapped_path = ResourceLoader::path_remap(p_path).simplify_path();
	const String extension = remapped_path.get_extension().to_lower();
	if (extension == "fsc" || extension == "fsb") {
		return remapped_path.get_basename() + ".fs";
	}
	return FoundryScript::canonicalize_path(remapped_path);
}

String normalized_manifest_path(const String &p_path) {
	return p_path.simplify_path();
}

void add_graph_diagnostic(Vector<FSNameManglerExport::Diagnostic> &r_diagnostics,
		const String &p_source, const String &p_message) {
	FSNameManglerExport::Diagnostic diagnostic;
	diagnostic.stage = "graph";
	diagnostic.source = p_source;
	diagnostic.message = p_message;
	r_diagnostics.push_back(diagnostic);
}

void add_stage_diagnostic(
		Vector<FSNameManglerExport::Diagnostic> &r_diagnostics,
		const String &p_stage, const String &p_source,
		const String &p_message) {
	FSNameManglerExport::Diagnostic diagnostic;
	diagnostic.stage = p_stage;
	diagnostic.source = p_source;
	diagnostic.message = p_message;
	r_diagnostics.push_back(diagnostic);
}

void add_binding_diagnostics(
		const Vector<FSNameManglerBindingSafety::Diagnostic> &p_diagnostics,
		Vector<FSNameManglerExport::Diagnostic> &r_diagnostics) {
	for (const FSNameManglerBindingSafety::Diagnostic &source_diagnostic :
			p_diagnostics) {
		String message = source_diagnostic.context;
		if (!source_diagnostic.message.is_empty()) {
			if (!message.is_empty()) {
				message += ": ";
			}
			message += source_diagnostic.message;
		}
		add_stage_diagnostic(r_diagnostics, "binding",
				source_diagnostic.source, message);
	}
}

void add_rule_diagnostics(
		const Vector<FSNameManglerKeepRules::Diagnostic> &p_diagnostics,
		Vector<FSNameManglerExport::Diagnostic> &r_diagnostics) {
	for (const FSNameManglerKeepRules::Diagnostic &source_diagnostic :
			p_diagnostics) {
		add_stage_diagnostic(
				r_diagnostics, "rules", source_diagnostic.source,
				vformat("line %d: %s: %s", source_diagnostic.line,
						source_diagnostic.severity ==
										FSNameManglerKeepRules::DIAGNOSTIC_WARNING
								? "warning"
								: "error",
						source_diagnostic.message));
	}
}

void sort_and_deduplicate_diagnostics(Vector<FSNameManglerExport::Diagnostic> &r_diagnostics) {
	r_diagnostics.sort_custom<ExportDiagnosticComparator>();
	for (int i = r_diagnostics.size() - 1; i > 0; i--) {
		const FSNameManglerExport::Diagnostic &left = r_diagnostics[i - 1];
		const FSNameManglerExport::Diagnostic &right = r_diagnostics[i];
		if (left.stage == right.stage && left.source == right.source && left.message == right.message) {
			r_diagnostics.remove_at(i);
		}
	}
}

Vector<String> sorted_unique_paths(const Vector<String> &p_paths) {
	Vector<String> paths = p_paths;
	paths.sort();
	for (int i = paths.size() - 1; i > 0; i--) {
		if (paths[i - 1] == paths[i]) {
			paths.remove_at(i);
		}
	}
	return paths;
}

String resolve_keep_rules_path(const String &p_path) {
	String normalized_path = p_path.replace("\\", "/").simplify_path();
	if (normalized_path.is_relative_path()) {
		normalized_path = "res://" + normalized_path;
	}
	return ProjectSettings::get_singleton()->globalize_path(normalized_path);
}

String compile_error_message(const String &p_source_path, Error p_error,
		const Ref<FoundryScript> &p_script) {
	Error parser_error = OK;
	const Ref<FSParserRef> parser_ref = FSCache::get_parser(
			p_source_path, FSParserRef::FULLY_SOLVED, parser_error);
	if (parser_ref.is_valid() && parser_ref->get_parser() != nullptr) {
		const List<FSParser::ParserError> &errors =
				parser_ref->get_parser()->get_errors();
		if (!errors.is_empty()) {
			const FSParser::ParserError &first_error =
					errors.front()->get();
			return vformat("Script could not be loaded: %s (line %d).",
					first_error.message, first_error.line);
		}
	}
	if (p_error != OK) {
		return vformat("Script could not be loaded: %s.", error_names[p_error]);
	}
	if (p_script.is_null()) {
		return "Script could not be loaded.";
	}
	return "Script could not be loaded because it is invalid.";
}

Error discover_script(const String &p_source_path, DiscoveredScript &r_discovered,
		Vector<FSNameManglerExport::Diagnostic> &r_diagnostics) {
	Error error = OK;
	const Ref<FoundryScript> script = FSCache::get_full_script(
			p_source_path, error, String(), true);
	if (error != OK || script.is_null() || !script->is_valid()) {
		add_graph_diagnostic(r_diagnostics, p_source_path,
				compile_error_message(p_source_path, error, script));
		return error != OK ? error : ERR_INVALID_DATA;
	}

	r_discovered.source_path = p_source_path;
	r_discovered.script = script;

	const String remapped_path = ResourceLoader::path_remap(p_source_path);
	if (remapped_path.get_extension().to_lower() == "fsb") {
		const Vector<uint8_t> buffer = FileAccess::get_file_as_bytes(remapped_path);
		if (buffer.is_empty()) {
			add_graph_diagnostic(r_diagnostics, p_source_path,
					"Compiled script dependencies could not be read: file is empty or unavailable.");
			return ERR_FILE_CANT_READ;
		}
		FSBytecodeLoader loader;
		error = loader.read_dependencies(buffer, r_discovered.dependencies);
		if (error != OK) {
			add_graph_diagnostic(r_diagnostics, p_source_path,
					vformat("Compiled script dependencies could not be read: %s.", error_names[error]));
			return error;
		}
		r_discovered.annotated_static_unload =
				loader.get_annotated_static_unload();
	} else {
		Error parser_error = OK;
		const Ref<FSParserRef> parser_ref = FSCache::get_parser(
				p_source_path, FSParserRef::PARSED, parser_error);
		if (parser_error != OK || parser_ref.is_null() ||
				parser_ref->get_parser() == nullptr ||
				parser_ref->get_parser()->get_tree() == nullptr) {
			add_graph_diagnostic(r_diagnostics, p_source_path,
					vformat("Script parser state could not be loaded: %s.",
							parser_error == OK ? "invalid parser state" : error_names[parser_error]));
			return parser_error != OK ? parser_error : ERR_INVALID_DATA;
		}
		for (const String &dependency :
				parser_ref->get_parser()->get_dependencies()) {
			r_discovered.dependencies.push_back(dependency);
		}
		r_discovered.dependencies.sort();
		r_discovered.annotated_static_unload =
				parser_ref->get_parser()->get_tree()->annotated_static_unload;
	}

	Vector<StringName> unsupported_globals =
			FSBytecodeExporter::collect_unsupported_named_globals(script);
	unsupported_globals.sort();
	if (!unsupported_globals.is_empty()) {
		Vector<String> printable_names;
		for (const StringName &name : unsupported_globals) {
			printable_names.push_back(String(name));
		}
		add_graph_diagnostic(r_diagnostics, p_source_path,
				vformat("Script references named globals that exist only in this editor session and are not defined by an exported game's runtime: %s.",
						String(", ").join(printable_names)));
		return ERR_INVALID_DATA;
	}

	return OK;
}

Error discover_graph(const FSNameManglerExport::Input &p_input,
		DiscoveredGraph &r_graph,
		Vector<FSNameManglerExport::Diagnostic> &r_diagnostics) {
	const Vector<String> paths = sorted_unique_paths(p_input.manifest_paths);
	HashSet<String> normalized_manifest_paths;
	HashSet<String> canonical_manifest_scripts;
	HashMap<String, String> first_source_by_canonical_script;
	Vector<String> script_paths;
	Vector<String> resource_paths;
	Error first_error = OK;

	for (const String &path : paths) {
		if (path.is_empty()) {
			add_graph_diagnostic(r_diagnostics, path, "Manifest path is empty.");
			if (first_error == OK) {
				first_error = ERR_INVALID_PARAMETER;
			}
			continue;
		}

		normalized_manifest_paths.insert(normalized_manifest_path(path));
		const String resource_type = ResourceLoader::get_resource_type(path);
		if (resource_type == "FoundryScript") {
			const String canonical_path = canonical_script_path(path);
			canonical_manifest_scripts.insert(canonical_path);
			if (const String *first_source =
							first_source_by_canonical_script.getptr(canonical_path)) {
				add_graph_diagnostic(r_diagnostics, path,
						vformat("Duplicate canonical script identity \"%s\" was already provided by \"%s\".",
								canonical_path, *first_source));
				if (first_error == OK) {
					first_error = ERR_ALREADY_EXISTS;
				}
				continue;
			}
			first_source_by_canonical_script.insert(canonical_path, path);
			script_paths.push_back(path);
		} else if (!resource_type.is_empty()) {
			resource_paths.push_back(path);
		}
	}

	for (const String &path : script_paths) {
		DiscoveredScript discovered;
		const Error error = discover_script(path, discovered, r_diagnostics);
		if (error != OK) {
			if (first_error == OK) {
				first_error = error;
			}
			continue;
		}
		r_graph.scripts.insert(path, discovered);
	}

	for (const KeyValue<String, DiscoveredScript> &entry : r_graph.scripts) {
		for (const String &dependency : entry.value.dependencies) {
			const String normalized_dependency = normalized_manifest_path(dependency);
			const String dependency_type =
					ResourceLoader::get_resource_type(dependency);
			const bool dependency_is_script =
					dependency_type == "FoundryScript";
			const bool dependency_present =
					normalized_manifest_paths.has(normalized_dependency) ||
					(dependency_is_script &&
							canonical_manifest_scripts.has(
									canonical_script_path(dependency)));
			if (!dependency_present) {
				add_graph_diagnostic(r_diagnostics, entry.key,
						vformat("Script dependency \"%s\" is outside the sealed export manifest.",
								dependency));
				if (first_error == OK) {
					first_error = ERR_DOES_NOT_EXIST;
				}
			}
		}
	}

	for (const String &path : resource_paths) {
		Error error = OK;
		const Ref<Resource> resource = ResourceLoader::load(
				path, String(), ResourceFormatLoader::CACHE_MODE_IGNORE_DEEP,
				&error);
		if (error != OK || resource.is_null()) {
			add_graph_diagnostic(r_diagnostics, path,
					vformat("Resource could not be loaded: %s.",
							error == OK ? "invalid resource" : error_names[error]));
			if (first_error == OK) {
				first_error = error != OK ? error : ERR_INVALID_DATA;
			}
			continue;
		}
		DiscoveredResource discovered;
		discovered.source_path = path;
		discovered.resource = resource;
		r_graph.resources.push_back(discovered);
	}

	sort_and_deduplicate_diagnostics(r_diagnostics);
	return first_error;
}

} // namespace

String FSNameManglerExport::Diagnostic::format() const {
	if (source.is_empty()) {
		return vformat("[%s] %s", stage, message);
	}
	return vformat("[%s] %s: %s", stage, source, message);
}

FSNameManglerExport::Result FSNameManglerExport::prepare(
		const Input &p_input) {
	Result failed_result;
	// Keep this scope declared before every graph/application owner. Their destructors must run
	// before the scope restores ordinary editor compilation and replays the recorded reloads.
	FSExportCompilationScope compilation_scope(p_input.release_profile);
	if (!compilation_scope.is_valid()) {
		failed_result.error = ERR_BUSY;
		add_graph_diagnostic(failed_result.diagnostics, String(),
				"Another Foundry Script export compilation is already active.");
		return failed_result;
	}

	DiscoveredGraph graph;
	const Error graph_error = discover_graph(
			p_input, graph, failed_result.diagnostics);
	if (graph_error != OK) {
		failed_result.error = graph_error;
		return failed_result;
	}

	FSNameManglerAnalysis::Input analysis_input;
	analysis_input.complete_project_graph = true;
	for (const KeyValue<String, DiscoveredScript> &entry : graph.scripts) {
		analysis_input.scripts.push_back(entry.value.script);
	}

	FSNameManglerBindingSafety::Input binding_input;
	for (const DiscoveredResource &resource : graph.resources) {
		binding_input.add_resource(resource.resource, resource.source_path);
	}
	const FSNameManglerBindingSafety::Result binding =
			FSNameManglerBindingSafety::collect(
					binding_input, analysis_input);
	add_binding_diagnostics(
			binding.diagnostics, failed_result.diagnostics);
	Error binding_apply_error = OK;
	if (binding.error == OK && binding.complete) {
		binding_apply_error = binding.apply_to_input(analysis_input);
	}
	if (binding.error != OK || !binding.complete ||
			binding_apply_error != OK) {
		failed_result.error =
				binding.error != OK
				? binding.error
				: binding_apply_error != OK
				? binding_apply_error
				: ERR_INVALID_DATA;
		if (binding.diagnostics.is_empty()) {
			add_stage_diagnostic(
					failed_result.diagnostics, "binding", String(),
					"Binding evidence collection was incomplete.");
		}
		sort_and_deduplicate_diagnostics(
				failed_result.diagnostics);
		return failed_result;
	}

	if (!p_input.keep_rules_path.is_empty()) {
		const String rules_path =
				resolve_keep_rules_path(p_input.keep_rules_path);
		FSNameManglerKeepRules rules;
		Vector<FSNameManglerKeepRules::Diagnostic> rule_diagnostics;
		const Error load_error = FSNameManglerKeepRules::load(
				rules_path, rules, rule_diagnostics);
		if (load_error != OK) {
			add_rule_diagnostics(
					rule_diagnostics, failed_result.diagnostics);
			failed_result.error = load_error;
			if (rule_diagnostics.is_empty()) {
				add_stage_diagnostic(
						failed_result.diagnostics, "rules", rules_path,
						vformat("Keep rules could not be loaded: %s.",
								error_names[load_error]));
			}
			sort_and_deduplicate_diagnostics(
					failed_result.diagnostics);
			return failed_result;
		}

		const Error apply_error =
				rules.apply_to_input(analysis_input, rule_diagnostics);
		add_rule_diagnostics(
				rule_diagnostics, failed_result.diagnostics);
		if (apply_error != OK) {
			failed_result.error = apply_error;
			if (rule_diagnostics.is_empty()) {
				add_stage_diagnostic(
						failed_result.diagnostics, "rules", rules_path,
						vformat("Keep rules could not be applied: %s.",
								error_names[apply_error]));
			}
			sort_and_deduplicate_diagnostics(
					failed_result.diagnostics);
			return failed_result;
		}
	}

	const FSNameManglerAnalysis::Result analysis =
			FSNameManglerAnalysis::analyze(analysis_input);
	if (analysis.error != OK) {
		failed_result.error = analysis.error;
		add_stage_diagnostic(
				failed_result.diagnostics, "analysis", String(),
				vformat("Name analysis failed: %s.",
						error_names[analysis.error]));
		sort_and_deduplicate_diagnostics(
				failed_result.diagnostics);
		return failed_result;
	}

	sort_and_deduplicate_diagnostics(failed_result.diagnostics);
	Result result;
	result.keep_log = analysis.keep_log;
	result.diagnostics = failed_result.diagnostics;
	for (const KeyValue<String, DiscoveredScript> &entry : graph.scripts) {
		PreparedScript prepared;
		prepared.source_path = entry.key;
		if (entry.key.get_extension().to_lower() == "fsb") {
			prepared.output_path = entry.key;
			prepared.remap = false;
		} else {
			prepared.output_path = entry.key.get_basename() + ".fsb";
			prepared.remap = true;
		}
		FSBytecodeExporter exporter;
		const Error serialization_error = exporter.serialize(
				entry.value.script, prepared.bytes,
				entry.value.annotated_static_unload);
		if (serialization_error != OK || prepared.bytes.is_empty()) {
			Result failed_serialization;
			failed_serialization.error = serialization_error != OK
					? serialization_error
					: ERR_INVALID_DATA;
			failed_serialization.diagnostics = result.diagnostics;
			Diagnostic diagnostic;
			diagnostic.stage = "serialization";
			diagnostic.source = entry.key;
			diagnostic.message = vformat(
					"Script could not be staged as compiled bytecode: %s.",
					serialization_error == OK
							? "serializer produced no data"
							: error_names[serialization_error]);
			failed_serialization.diagnostics.push_back(diagnostic);
			sort_and_deduplicate_diagnostics(
					failed_serialization.diagnostics);
			return failed_serialization;
		}
		result.scripts.insert(entry.key, prepared);
	}
	return result;
}

bool FSNameManglerExport::is_sensitive_generated_path(
		const String &p_path) {
	const String extension = p_path.get_extension().to_lower();
	return extension == "fs" || extension == "fsc" || extension == "fsb" ||
			extension == "tscn" || extension == "scn" ||
			extension == "tres" || extension == "res";
}

#endif // TOOLS_ENABLED
