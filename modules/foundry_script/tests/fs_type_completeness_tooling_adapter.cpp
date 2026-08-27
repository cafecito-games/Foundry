/**************************************************************************/
/*  fs_type_completeness_tooling_adapter.cpp                              */
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

#include "fs_type_completeness_tooling_adapter.h"

#include "core/io/json.h"

#ifdef FS_COMPLETENESS_TOOLING_ADAPTER_AVAILABLE
#include "fs_test_language_lifecycle.h"
#include "fs_type_completeness_case_id.h"
#include "fs_type_completeness_destination_wrapper_adapter.h"

#include "../fs_analyzer.h"
#include "../fs_bytecode_export.h"
#include "../fs_bytecode_loader.h"
#include "../fs_cache.h"
#include "../fs_compiler.h"
#include "../fs_parser.h"
#include "../language_server/fs_extend_parser.h"
#include "../language_server/fs_language_protocol.h"

#include "core/os/mutex.h"
#endif // FS_COMPLETENESS_TOOLING_ADAPTER_AVAILABLE

namespace FSTests {

namespace {

// Two renderings that differ only in the spacing a renderer is free to choose are the one difference
// the contract lets a family defer, so the comparison needs a form in which that spacing is gone.
// Nothing else is normalized: a different type name, a different nullability marker, or a missing
// rendering all survive this and are reported as a real disagreement.
String collapsed_spacing(const String &p_rendering) {
	String collapsed;
	for (int index = 0; index < p_rendering.length(); index++) {
		const char32_t character = p_rendering[index];
		if (character != ' ' && character != '\t') {
			collapsed += String::chr(character);
		}
	}
	return collapsed;
}

} // namespace

String tooling_adapter_id() {
	return "tooling";
}

Vector<String> tooling_actions() {
	return Vector<String>({ "completion", "diagnostic_rendering", "documentation", "hover", "migration",
			"navigation", "project_scan", "rename_refactor", "signature_help" });
}

Vector<String> tooling_driven_actions() {
	return Vector<String>({ "hover" });
}

Vector<String> tooling_destinations() {
	return Vector<String>({ "plain", "union", "optional", "container_element", "tuple_field" });
}

HashMap<String, Vector<String>> tooling_renderable_leaves() {
	HashMap<String, Vector<String>> leaves;
	leaves["destination"] = tooling_destinations();
	leaves["tooling_action"] = tooling_driven_actions();
	leaves["surface"] = Vector<String>({ "text", "bytecode" });
	return leaves;
}

HashSet<String> tooling_observable_dimensions() {
	return HashSet<String>({ "tooling_parity", "tooling_outcome" });
}

String compare_tooling_evidence(const FSCompletenessToolingEvidence &p_evidence) {
	// A tooling surface that rendered nothing agreed with nothing. Treating an empty rendering as a
	// wording difference would let a surface that lost the type entirely report the mildest outcome
	// the vocabulary has.
	if (p_evidence.tooling_rendered_type.is_empty() || p_evidence.analyzer_rendered_type.is_empty()) {
		return "type_differs";
	}
	if (p_evidence.tooling_rendered_type == p_evidence.analyzer_rendered_type) {
		return "agrees";
	}
	return collapsed_spacing(p_evidence.tooling_rendered_type) ==
					collapsed_spacing(p_evidence.analyzer_rendered_type)
			? "wording_differs"
			: "type_differs";
}

Error parse_tooling_host_readiness(
		const String &p_line, FSCompletenessToolingHostReadiness &r_readiness) {
	r_readiness = FSCompletenessToolingHostReadiness();
	const String line = p_line.strip_edges();
	const String prefix = "FOUNDRY_TOOLING ";
	if (!line.begins_with(prefix)) {
		return ERR_UNAVAILABLE;
	}
	Ref<JSON> json;
	json.instantiate();
	if (json->parse(line.substr(prefix.length())) != OK) {
		return ERR_UNAVAILABLE;
	}
	const Variant parsed = json->get_data();
	if (parsed.get_type() != Variant::DICTIONARY) {
		return ERR_UNAVAILABLE;
	}
	const Dictionary record = parsed;
	const Variant lsp_port = record.get("lsp_port", Variant());
	const Variant dap_port = record.get("dap_port", Variant());
	if (!lsp_port.is_num() || !dap_port.is_num()) {
		return ERR_UNAVAILABLE;
	}
	FSCompletenessToolingHostReadiness readiness;
	readiness.lsp_port = int(lsp_port);
	readiness.dap_port = int(dap_port);
	readiness.local_only = bool(record.get("local_only", false));
	// A port a listener never bound is announced as a negative number, and a host reachable from off
	// the machine is not the loopback-only host this harness is allowed to drive.
	if (readiness.lsp_port <= 0 || readiness.dap_port <= 0 || !readiness.local_only) {
		return ERR_UNAVAILABLE;
	}
	r_readiness = readiness;
	return OK;
}

String rendered_type_in_hover_contents(const String &p_contents, const String &p_member_name) {
	if (p_contents.is_empty() || p_member_name.is_empty()) {
		return String();
	}
	const String declaration = vformat("var %s", p_member_name);
	const int declared_at = p_contents.find(declaration);
	if (declared_at < 0) {
		return String();
	}
	const int separator_at = declared_at + declaration.length();
	// The member name has to end where the declaration does, or `value` would read the type of
	// `value_count`.
	if (!p_contents.substr(separator_at).begins_with(": ")) {
		return String();
	}
	const int type_at = separator_at + 2;
	int type_end = p_contents.find("\n", type_at);
	if (type_end < 0) {
		type_end = p_contents.length();
	}
	const int initializer_at = p_contents.find(" = ", type_at);
	if (initializer_at >= 0 && initializer_at < type_end) {
		type_end = initializer_at;
	}
	return p_contents.substr(type_at, type_end - type_at).strip_edges();
}

#ifdef FS_COMPLETENESS_TOOLING_ADAPTER_AVAILABLE

namespace {

static thread_local bool blank_tooling_rendering_for_test = false;
static thread_local bool tooling_host_unavailable_for_test = false;
static thread_local bool respaced_tooling_rendering_for_test = false;

// The class the rendered program declares the carried member in, and the member itself. Both the
// tooling surface and the analyzer are asked about this one member, so a cell compares two answers
// to the same question rather than two questions.
static const char *tooling_carrier_class = "Holder";
static const char *tooling_carried_member = "value";

// One member type a tooling surface has to render, and a value it can be initialized with. The
// spelling is the declaration the program contains; the observation renders what the tooling surface
// and the analyzer each say about it and compares them, so nothing here is an expected outcome.
struct ToolingDestinationShape {
	const char *leaf;
	const char *spelling;
	const char *initializer;
};

static const ToolingDestinationShape tooling_destination_shapes[] = {
	{ "plain", "uint", "0U" },
	{ "union", "uint | String", "0U" },
	{ "optional", "uint?", "null" },
	{ "container_element", "Array[uint]", "[0U]" },
	{ "tuple_field", "(uint, String)", "(0U, \"a\")" },
};

static const ToolingDestinationShape *find_tooling_destination_shape(const String &p_leaf) {
	for (const ToolingDestinationShape &shape : tooling_destination_shapes) {
		if (p_leaf == shape.leaf) {
			return &shape;
		}
	}
	return nullptr;
}

static bool read_tooling_coordinate(
		const Dictionary &p_coordinates, const String &p_axis, String &r_value) {
	const Variant value = p_coordinates.get(p_axis, Variant());
	if (value.get_type() != Variant::STRING) {
		return false;
	}
	r_value = value;
	return !r_value.is_empty();
}

class ToolingBytecodeResolver : public FSBytecodeExternalResolver {
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
// has. The language is process-wide and the registry hands one adapter instance to every run, so the
// decision to bring it up and the decision to take it down are counted under one lock.
class ToolingLanguageBoot {
	static Mutex &boot_mutex() {
		static Mutex mutex;
		return mutex;
	}

	static int &boot_depth() {
		static int depth = 0;
		return depth;
	}

	static bool &boot_owns_language() {
		static bool owns = false;
		return owns;
	}

public:
	ToolingLanguageBoot() {
		MutexLock lock(boot_mutex());
		if (boot_depth() == 0) {
			boot_owns_language() = ensure_fs_language_initialized();
		}
		boot_depth()++;
	}

	~ToolingLanguageBoot() {
		MutexLock lock(boot_mutex());
		boot_depth()--;
		if (boot_depth() == 0 && boot_owns_language()) {
			boot_owns_language() = false;
			FSLanguage::get_singleton()->finish();
		}
	}

	ToolingLanguageBoot(const ToolingLanguageBoot &) = delete;
	ToolingLanguageBoot &operator=(const ToolingLanguageBoot &) = delete;
};

// The tooling helpers reach the language server's own singleton to render a symbol's URI, so a cell
// that drives them outside a running server has to stand one up. It is brought up once for the work
// a scope covers and taken back down with it, and a server this process already runs - a language
// server test, or a real editor session - is used as it is rather than replaced.
class ToolingProtocolScope {
	static Mutex &scope_mutex() {
		static Mutex mutex;
		return mutex;
	}

	static int &scope_depth() {
		static int depth = 0;
		return depth;
	}

	static FSLanguageProtocol *&owned_protocol() {
		static FSLanguageProtocol *protocol = nullptr;
		return protocol;
	}

	bool available = false;

public:
	explicit ToolingProtocolScope(const String &p_workspace_root) {
		MutexLock lock(scope_mutex());
		if (scope_depth() == 0 && FSLanguageProtocol::get_singleton() == nullptr) {
			owned_protocol() = memnew(FSLanguageProtocol);
		}
		scope_depth()++;
		FSLanguageProtocol *protocol = FSLanguageProtocol::get_singleton();
		if (protocol == nullptr) {
			return;
		}
		available = true;
		if (owned_protocol() == nullptr) {
			// Somebody else's server: its workspace describes their project, and moving it under this
			// cell's staged sources would break whatever they are in the middle of.
			return;
		}
		Ref<FSWorkspace> workspace = protocol->get_workspace();
		if (workspace.is_valid()) {
			workspace->root = p_workspace_root;
			workspace->root_uri = "file:///" + p_workspace_root.lstrip("/").replace_first(":", "%3A");
		}
	}

	~ToolingProtocolScope() {
		MutexLock lock(scope_mutex());
		scope_depth()--;
		if (scope_depth() == 0 && owned_protocol() != nullptr) {
			memdelete(owned_protocol());
			owned_protocol() = nullptr;
		}
	}

	bool is_available() const { return available; }

	ToolingProtocolScope(const ToolingProtocolScope &) = delete;
	ToolingProtocolScope &operator=(const ToolingProtocolScope &) = delete;
};

static void append_tooling_diagnostic(
		FSCompletenessObservation &r_observation, const String &p_code, const String &p_message) {
	r_observation.diagnostics.push_back(p_message);
	Dictionary record;
	record["severity"] = "error";
	record["category"] = "tooling";
	record["code"] = p_code;
	record["line"] = 0.0;
	record["column"] = 0.0;
	record["message"] = p_message;
	record["suppressed"] = false;
	r_observation.diagnostic_records.push_back(record);
}

static String render_tooling_source(const ToolingDestinationShape &p_shape) {
	String source = vformat("class %s:\n", tooling_carrier_class);
	source += vformat("\tvar %s: %s = %s\n", tooling_carried_member, p_shape.spelling, p_shape.initializer);
	source += "\nfunc test() -> void:\n";
	source += vformat("\tvar holder: %s = %s.new()\n", tooling_carrier_class, tooling_carrier_class);
	source += vformat("\tprint(holder.%s)\n", tooling_carried_member);
	return source;
}

static Error compile_tooling_source(
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

// The declared type of the carried member as the analyzer resolved it, read off the analyzed program
// itself. This is the answer a tooling surface is supposed to be showing, so it is what the text
// surface compares against.
static String analyzed_member_type(const FSParser &p_parser) {
	const FSParser::ClassNode *root = p_parser.get_tree();
	if (root == nullptr) {
		return String();
	}
	for (int index = 0; index < root->members.size(); index++) {
		const FSParser::ClassNode::Member &member = root->members[index];
		if (member.type != FSParser::ClassNode::Member::CLASS || member.m_class == nullptr ||
				member.m_class->identifier == nullptr ||
				member.m_class->identifier->name != StringName(tooling_carrier_class)) {
			continue;
		}
		for (int carried_index = 0; carried_index < member.m_class->members.size(); carried_index++) {
			const FSParser::ClassNode::Member &carried = member.m_class->members[carried_index];
			if (carried.type != FSParser::ClassNode::Member::VARIABLE || carried.variable == nullptr ||
					carried.variable->identifier == nullptr ||
					carried.variable->identifier->name != StringName(tooling_carried_member)) {
				continue;
			}
			return carried.get_datatype().to_string();
		}
	}
	return String();
}

// The declared type of the carried member as a compiled artifact records it. An empty string means
// the artifact has no such member at all, which is a lost type rather than a rendered one.
static String analyzer_rendered_type(const Ref<FoundryScript> &p_script) {
	if (p_script.is_null()) {
		return String();
	}
	const Ref<FoundryScript> *carrier = p_script->get_subclasses().getptr(StringName(tooling_carrier_class));
	if (carrier == nullptr || carrier->is_null()) {
		return String();
	}
	const FSDataType *carried = (*carrier)->find_member_data_type(StringName(tooling_carried_member));
	return carried == nullptr ? String() : carried->get_source_type_name();
}

// Restores an exported artifact, so a bytecode-surface cell compares the tooling surface against the
// type the shipped binary carries rather than against the one a source compile just produced.
static Error restore_tooling_artifact(
		const Vector<uint8_t> &p_buffer, const String &p_path, Ref<FoundryScript> &r_restored) {
	r_restored.unref();
	if (p_buffer.is_empty()) {
		return ERR_INVALID_DATA;
	}
	Ref<FoundryScript> restored;
	restored.instantiate();
	restored->set_path_cache(p_path);
	ToolingBytecodeResolver resolver;
	FSBytecodeLoader loader;
	loader.set_resolver(&resolver);
	Error error = loader.load_skeleton(p_buffer, restored);
	if (error == OK) {
		error = loader.load_full(p_buffer, restored);
	}
	if (error != OK) {
		return error;
	}
	if (!restored->is_valid() || !restored->is_compiled_binary()) {
		return ERR_INVALID_DATA;
	}
	r_restored = restored;
	return OK;
}

// What the tooling surface shows a user for the carried member.
//
// The subject is the document symbol the language server builds for the program and the markdown
// body `DocumentSymbol::render()` produces from it, which is verbatim what `FSTextDocument::hover`
// returns once a symbol has been resolved. What this does not cover is the step before that:
// `FSWorkspace::resolve_symbol` turning a cursor position into that symbol, and the JSON-RPC
// response built around it. Driving those needs a workspace rooted in a real project and a
// connected client, which a completeness run does not have, so a resolution defect would still be
// reported here as a rendered hover. The family therefore observes the type a hover shows, not the
// request path that delivers it; extending it to the request path is tracked separately.
static String tooling_rendered_type(const String &p_source, const String &p_path, Error &r_error) {
	r_error = OK;
	if (tooling_host_unavailable_for_test) {
		r_error = ERR_UNAVAILABLE;
		return String();
	}
	ToolingProtocolScope protocol(p_path.get_base_dir());
	if (!protocol.is_available()) {
		r_error = ERR_UNAVAILABLE;
		return String();
	}
	ExtendFSParser *tooling_parser = ExtendFSParser::parse_source(p_source, p_path);
	if (tooling_parser == nullptr) {
		r_error = ERR_UNAVAILABLE;
		return String();
	}
	String rendered;
	const ClassMembers *carrier =
			tooling_parser->get_inner_classes().getptr(String(tooling_carrier_class));
	if (carrier != nullptr) {
		const LSP::DocumentSymbol *const *carried = carrier->getptr(String(tooling_carried_member));
		if (carried != nullptr && *carried != nullptr) {
			rendered = rendered_type_in_hover_contents(
					(*carried)->render().value, tooling_carried_member);
		}
	}
	memdelete(tooling_parser);
	if (blank_tooling_rendering_for_test) {
		return String();
	}
	if (respaced_tooling_rendering_for_test && !rendered.is_empty()) {
		return rendered.replace(" ", "  ");
	}
	return rendered;
}

} // namespace

void ToolingInternal::set_blank_tooling_rendering_for_test(bool p_blank) {
	blank_tooling_rendering_for_test = p_blank;
}

void ToolingInternal::set_tooling_host_unavailable_for_test(bool p_unavailable) {
	tooling_host_unavailable_for_test = p_unavailable;
}

void ToolingInternal::set_respaced_tooling_rendering_for_test(bool p_respaced) {
	respaced_tooling_rendering_for_test = p_respaced;
}

const FSToolingAdapter &FSToolingAdapter::shared() {
	static FSToolingAdapter adapter;
	return adapter;
}

Vector<String> FSToolingAdapter::families() {
	Vector<String> registered;
	for (const String &action : tooling_driven_actions()) {
		registered.push_back(vformat("tooling_%s", action));
	}
	registered.sort();
	return registered;
}

String FSToolingAdapter::id() const {
	return tooling_adapter_id();
}

HashSet<String> FSToolingAdapter::observable_dimensions() const {
	return tooling_observable_dimensions();
}

HashMap<String, Vector<String>> FSToolingAdapter::renderable_leaves() const {
	return tooling_renderable_leaves();
}

Error FSToolingAdapter::render(
		const FSCompletenessResolvedCell &p_cell, FSCompletenessProgram &r_program) const {
	r_program = FSCompletenessProgram();
	String destination;
	String action;
	String surface;
	if (p_cell.case_id.is_empty() ||
			!read_tooling_coordinate(p_cell.coordinates, "destination", destination) ||
			!read_tooling_coordinate(p_cell.coordinates, "tooling_action", action) ||
			!read_tooling_coordinate(p_cell.coordinates, "surface", surface)) {
		return ERR_INVALID_DATA;
	}
	if (!can_render("destination", destination) || !can_render("tooling_action", action) ||
			!can_render("surface", surface)) {
		return ERR_INVALID_DATA;
	}
	const ToolingDestinationShape *shape = find_tooling_destination_shape(destination);
	if (shape == nullptr) {
		return ERR_INVALID_DATA;
	}
	FSCompletenessProgram program;
	program.case_id = p_cell.case_id;
	program.surface = surface;
	program.coordinates = p_cell.coordinates.duplicate();
	program.source = render_tooling_source(*shape);
	// The evidence of a tooling cell is what the tooling surface and the analyzer each say about the
	// carried member, not what the program printed, so the program is never run and both sides of the
	// output comparison are empty.
	program.expected_output = String();
	r_program = program;
	return OK;
}

FSCompletenessObservation FSToolingAdapter::analyze(
		const FSCompletenessProgram &p_program, const String &p_surface) const {
	FSCompletenessObservation observation;
	observation.case_id = p_program.case_id;
	observation.surface = p_surface;
	if (p_program.surface != p_surface) {
		append_tooling_diagnostic(observation, "surface_mismatch",
				vformat("Program surface '%s' does not match observed surface '%s'.", p_program.surface,
						p_surface));
		return observation;
	}
	return observe_tooling_surface(p_program, nullptr);
}

FSCompletenessObservation FSToolingAdapter::observe_tooling_surface(
		const FSCompletenessProgram &p_program, Error *r_structural_error) const {
	if (r_structural_error != nullptr) {
		*r_structural_error = OK;
	}
	FSCompletenessObservation observation;
	observation.case_id = p_program.case_id;
	observation.surface = p_program.surface;
	String destination;
	if (!read_tooling_coordinate(p_program.coordinates, "destination", destination)) {
		append_tooling_diagnostic(
				observation, "coordinates_unreadable", "Program coordinates are unreadable.");
		if (r_structural_error != nullptr) {
			*r_structural_error = ERR_INVALID_DATA;
		}
		return observation;
	}
	if (find_tooling_destination_shape(destination) == nullptr) {
		append_tooling_diagnostic(observation, "destination_unknown",
				vformat("Destination '%s' has no rendered shape.", destination));
		if (r_structural_error != nullptr) {
			*r_structural_error = ERR_INVALID_DATA;
		}
		return observation;
	}

	ToolingLanguageBoot language;
	// The scope is held for the whole observation: it is what serializes this run against every other
	// user of a synthetic source, and both the analyzer side and the tooling side read the same
	// identity.
	DestinationWrapperInternal::SyntheticSourceScope synthetic_source(
			p_program.case_id, p_program.source);
	if (!synthetic_source.is_available()) {
		append_tooling_diagnostic(observation, "source_unavailable",
				"The program's source could not be staged for the tooling surface.");
		if (r_structural_error != nullptr) {
			*r_structural_error = ERR_CANT_CREATE;
		}
		return observation;
	}
	const String path = synthetic_source.get_path();

	FSParser parser;
	Ref<FoundryScript> compiled;
	Error error = compile_tooling_source(p_program.source, path, parser, compiled);
	if (error != OK) {
		append_tooling_diagnostic(observation, "program_uncompilable",
				vformat("The program could not be compiled (error %d).", error));
		if (r_structural_error != nullptr) {
			*r_structural_error = error;
		}
		return observation;
	}

	// What a cell holds the tooling surface to is its surface. The text surface asks whether tooling
	// shows the type the analyzer just resolved; the bytecode surface asks whether it shows the type
	// the shipped binary records, which is a different object and can answer differently.
	FSCompletenessToolingEvidence evidence;
	if (p_program.surface == "bytecode") {
		FSBytecodeExporter exporter;
		Vector<uint8_t> buffer;
		Ref<FoundryScript> restored;
		error = exporter.serialize(compiled, buffer);
		if (error == OK) {
			error = restore_tooling_artifact(buffer, path, restored);
		}
		if (error != OK) {
			append_tooling_diagnostic(observation, "subject_unavailable",
					vformat("The compiled binary under test could not be restored (error %d).", error));
			if (r_structural_error != nullptr) {
				*r_structural_error = error;
			}
			return observation;
		}
		evidence.analyzer_rendered_type = analyzer_rendered_type(restored);
	} else {
		evidence.analyzer_rendered_type = analyzed_member_type(parser);
	}
	Error tooling_error = OK;
	evidence.tooling_rendered_type = tooling_rendered_type(p_program.source, path, tooling_error);
	if (tooling_error != OK) {
		// A tooling surface that never came up observed nothing, and its silence is not evidence that
		// the type survived. It is a defect in the harness or in the host rather than in the product.
		append_tooling_diagnostic(observation, "tooling_host_unavailable",
				vformat("The tooling surface could not be driven (error %d).", tooling_error));
		if (r_structural_error != nullptr) {
			*r_structural_error = tooling_error;
		}
		return observation;
	}
	if (evidence.analyzer_rendered_type.is_empty()) {
		// Nothing to compare the tooling rendering against: the subject carries no such member at all,
		// which is a defect in what the cell selected rather than a disagreement about a type.
		append_tooling_diagnostic(observation, "analyzer_reading_absent",
				"The subject under test carries no declared type for the member the cell observes.");
		if (r_structural_error != nullptr) {
			*r_structural_error = ERR_INVALID_DATA;
		}
		return observation;
	}

	observation.dimensions["tooling_parity"] = compare_tooling_evidence(evidence);
	observation.dimensions["tooling_outcome"] =
			evidence.tooling_rendered_type.is_empty() ? "absent" : "rendered";
	// What each side actually rendered, published as evidence rather than as a verdict. A parity
	// finding is only classifiable by someone who can see the two spellings that disagreed.
	observation.dimensions["evidence.tooling_rendered_type"] = evidence.tooling_rendered_type;
	observation.dimensions["evidence.analyzer_rendered_type"] = evidence.analyzer_rendered_type;
	observation.produced_output = String();
	return observation;
}

Error FSToolingAdapter::execute(const String &p_scratch_root,
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
	// per program would pay for it once per cell.
	ToolingLanguageBoot language;
	FSCompletenessRuntimeBatch completed;
	for (const FSCompletenessProgram &program : p_programs) {
		if (program.case_id.is_empty() || program.source.is_empty()) {
			return ERR_INVALID_DATA;
		}
		if (batch_family.is_empty()) {
			for (const String &candidate : families()) {
				if (program.case_id == FSCompletenessCaseID::make(candidate, program.coordinates)) {
					batch_family = candidate;
					break;
				}
			}
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
		FSCompletenessObservation observation = observe_tooling_surface(program, &structural_error);
		if (structural_error != OK) {
			return structural_error;
		}
		FSCompletenessRuntimeResult result;
		static_cast<FSCompletenessObservation &>(result) = observation;
		result.passed = result.diagnostics.is_empty();
		result.status = result.passed ? "ok" : "tooling_rejected";
		results->insert(program.case_id, result);
	}
	r_batch = completed;
	return OK;
}

#endif // FS_COMPLETENESS_TOOLING_ADAPTER_AVAILABLE

} // namespace FSTests
