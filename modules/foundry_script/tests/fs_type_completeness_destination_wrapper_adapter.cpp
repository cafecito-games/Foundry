/**************************************************************************/
/*  fs_type_completeness_destination_wrapper_adapter.cpp                  */
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

#include "fs_type_completeness_destination_wrapper_adapter.h"

#include "fs_temporary_project_tree.h"
#include "fs_test_runner.h"
#include "fs_type_completeness_common.h"

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

using namespace Completeness;

namespace {

static const String destination_wrapper_adapter_id = "destination_wrapper";
static thread_local DestinationWrapperInternal::PersistedWriteTestHook persisted_write_test_hook = nullptr;
static thread_local bool blank_reflection_hint_for_test = false;

static bool read_coordinate(const Dictionary &p_coordinates, const String &p_axis, String &r_value) {
	const Variant value = p_coordinates.get(p_axis, Variant());
	if (value.get_type() != Variant::STRING) {
		return false;
	}
	r_value = value;
	return FSDestinationWrapperAdapter::shared().can_render(p_axis, r_value);
}

// One destination wrapper: how the program spells it, how it is initialized, how a source value is
// wrapped into it, how the transported wrapper is unwrapped again, and which `stored_carrier`
// outcome a provable source lands on there. Every axis of the rendered program is one of these
// tables, so a new leaf is a new row rather than a new branch.
struct DestinationShape {
	const char *leaf;
	const char *type_spelling;
	const char *default_expression;
	// Statements that stage the value the boundary writes, one per line, empty when the source
	// expression is already shaped like the destination. Nothing staged here may be typed as the
	// destination: a typed staging slot would perform the destination's own check before the boundary
	// under test ever saw the value, and the boundary would then only ever receive a proven one.
	const char *wrap_statements;
	// What the boundary writes: the source expression itself, or the untyped carrier staged above.
	const char *boundary_value;
	// Expression over `transported` yielding the value that crossed the destination; may name SOURCE
	// when the destination is a slot the value only reaches at use, as a callable parameter is.
	const char *unwrap_expression;
	// Annotation the unwrap statement needs when reaching the destination raises a warning the
	// program's own output would otherwise carry.
	const char *unwrap_annotation;
	const char *stored_carrier;
};

static const DestinationShape destination_shapes[] = {
	// A destination whose shape is the source's own shape takes the source expression straight across
	// the boundary: nothing between the source proof and the boundary re-types the value, so the
	// boundary under test is what performs the destination's check.
	{ "plain", "uint", "0U", "", "SOURCE", "transported", "", "plain_destination" },
	{ "union", "uint | String", "0U", "", "SOURCE", "transported", "", "admitting_alternative" },
	{ "optional", "uint?", "null", "", "SOURCE", "transported", "", "optional_payload" },
	// A wrapper destination has to be built before it can cross a boundary, and the language admits no
	// untyped carrier into a typed one: an `Array` is refused by an `Array[uint]` slot, and a raw
	// `Box.new()` keeps no argument to check against. The destination's own check therefore happens
	// where the wrapper is built - which is itself a store into that destination - and the boundary
	// transports a value already of the destination type. `stored_carrier` and the descriptor are still
	// read from the boundary's own slot, so a boundary that lost its record is still caught.
	{ "container_element", "Array[uint]", "[] as Array[uint]", "var wrapped: Array[uint] = [SOURCE]",
			"wrapped", "transported[0]", "", "element_slot" },
	{ "generic_argument", "Box[uint]", "Box[uint].new()",
			"var wrapped: Box[uint] = Box[uint].new()\nwrapped.value = SOURCE", "wrapped",
			"transported.value", "", "reified_argument" },
	{ "tuple_field", "(uint, String)", "(0U, \"\")", "var wrapped: (uint, String) = (SOURCE, \"tag\")",
			"wrapped", "transported[0]", "", "tuple_slot" },
	// The uint reaches a callable destination at the call, which happens after the boundary has
	// transported the callable, so the source crosses this destination unwrapped.
	{ "callable_slot", "Callable[[uint], Variant]", "Callable()",
			"var wrapped: Callable[[uint], Variant] = identity_uint", "wrapped", "transported.call(SOURCE)",
			"@warning_ignore(\"unsafe_call_argument\")", "callable_slot" },
	{ "nominal_class", "Carrier", "Carrier.new()",
			"var wrapped: Carrier = Carrier.new()\nwrapped.value = SOURCE", "wrapped", "transported.value",
			"", "nominal_instance" },
	{ "trait", "HasValue", "Carrier.new()", "var wrapped: HasValue = Carrier.new()\nwrapped.value = SOURCE",
			"wrapped", "transported.value", "", "trait_witness" },
};



// Where a boundary's own destination slot is recorded, and so where its descriptor is read back from.
// Every site is the slot the boundary actually writes through: an unrelated declaration of the same
// type would pass a program whose boundary lost its check.
enum DestinationSite {
	SITE_FUNCTION_ARGUMENT,
	SITE_FUNCTION_RETURN,
	SITE_CLASS_MEMBER,
	// The element type of a typed container held by a class member.
	SITE_CLASS_MEMBER_ELEMENT,
	// A typed local. The compiled script does not expose locals, so this one site is read from the
	// analyzer's own record of the same program.
	SITE_FUNCTION_LOCAL,
};

// One write boundary: the declarations it needs, the statements that carry `wrapped` across it into
// `transported`, and where the destination it wrote through is recorded. DESTINATION and DEFAULT are
// replaced with the destination's spelling and its initializer.
struct BoundaryShape {
	const char *leaf;
	const char *declarations;
	const char *statements;
	DestinationSite site;
	// Class that owns a member site, or function that owns an argument, return, or local site.
	const char *site_owner;
	// Member or local name; empty for an argument or return site.
	const char *site_name;
};

static const BoundaryShape boundary_shapes[] = {
	{ "argument_binding", "func accept_destination(value: DESTINATION) -> DESTINATION:\n\treturn value",
			"@warning_ignore(\"unsafe_call_argument\")\n"
			"var transported: DESTINATION = accept_destination(BOUNDARY_VALUE)",
			SITE_FUNCTION_ARGUMENT,
			"accept_destination", "" },
	{ "return", "func produce(value: Variant) -> DESTINATION:\n\treturn value",
			"var transported: DESTINATION = produce(BOUNDARY_VALUE)", SITE_FUNCTION_RETURN, "produce", "" },
	{ "assignment", "", "var transported: DESTINATION = DEFAULT\ntransported = BOUNDARY_VALUE",
			SITE_FUNCTION_LOCAL, "test", "transported" },
	{ "member_store", "class Holder extends RefCounted:\n\tvar value: DESTINATION = DEFAULT",
			"var holder := Holder.new()\nholder.value = BOUNDARY_VALUE\nvar transported: DESTINATION = holder.value",
			SITE_CLASS_MEMBER, "Holder", "value" },
	{ "container_element_store", "class Slots extends RefCounted:\n\tvar values: Array[DESTINATION] = [DEFAULT]",
			"var slots := Slots.new()\nslots.values[0] = BOUNDARY_VALUE\n"
			"var transported: DESTINATION = slots.values[0]",
			SITE_CLASS_MEMBER_ELEMENT, "Slots", "values" },
	{ "reflective_write", "class Holder extends RefCounted:\n\tvar value: DESTINATION = DEFAULT",
			"var holder := Holder.new()\n@warning_ignore(\"unsafe_call_argument\")\n"
			"holder.set(&\"value\", BOUNDARY_VALUE)\n"
			"var transported: DESTINATION = holder.value",
			SITE_CLASS_MEMBER, "Holder", "value" },
	{ "proxy_write",
			"class Proxy extends RefCounted:\n\tvar backing: DESTINATION = DEFAULT\n"
			"\tvar value: DESTINATION = DEFAULT:\n\t\tset(incoming):\n\t\t\tbacking = incoming\n"
			"\t\t\tvalue = incoming",
			"var proxy := Proxy.new()\nproxy.value = BOUNDARY_VALUE\nvar transported: DESTINATION = proxy.backing",
			SITE_CLASS_MEMBER, "Proxy", "value" },
};


// One source of the transported value and how provable it is at the destination.
struct SourceProofShape {
	const char *leaf;
	const char *expression;
	// Expression the wrapper-parity layout uses, when it must differ. A bare `5` reaches a typed local
	// as a provable constant but reaches a boundary through a `Variant` hop as an ordinary int, which
	// a typed return refuses; the wrapper layout spells the constant at its own width so the cell
	// measures the boundary rather than that separate question.
	const char *wrapper_expression;
	// True when the analyzer cannot prove the value's type at the store, so the destination owes a
	// runtime membership or conversion check.
	bool unproven;
	// True when the value's type is provable at the store, so the carrier it is stored in is decided
	// statically and is observable as `stored_carrier`.
	bool carrier_provable;
};

static const SourceProofShape source_proof_shapes[] = {
	{ "static_member", "typed_source", "typed_source", false, false },
	{ "numeric_constant", "5", "5U", false, true },
	{ "inferred", "inferred_source", "inferred_source", false, true },
	{ "gradual", "supply(5U)", "supply(5U)", true, false },
	{ "erased", "erase[uint](5U)", "erase[uint](5U)", true, false },
	{ "variant", "variant_source", "variant_source", true, false },
	{ "nested_child", "nested_source[0]", "nested_source[0]", true, false },
};


// One census child slot and the declaration that forces a program to realize it. The leaf ids are
// `<representation>.<child_slot>` pairs from `census/representations.json`; several representations
// describe the same declared shape at a different stage, so they legitimately share a declaration
// and are told apart by the representation the observation reads it from.
struct CensusWitnessShape {
	const char *leaf;
	const char *declarations;
	// Body statement that consumes the declaration. A declaration nothing reads could be elided before
	// it ever reaches the representation the coordinate names, so every witness is used.
	const char *use_statement;
	// Whether the adapter can read this child back out of the representation the leaf names. A leaf it
	// cannot read is not renderable: a cell whose named child is never observed would pass whatever
	// happened to that child, which is a coverage claim the run cannot support.
	bool observed;
};

static const char *census_container_witness = "var census_container: Array[uint] = [5U]";
static const char *census_container_use = "var _census_use: Variant = census_container";
static const char *census_arguments_witness = "var census_arguments: Box[uint] = Box[uint].new()";
static const char *census_arguments_use = "var _census_use: Variant = census_arguments.value";
static const char *census_union_witness = "var census_union: uint | String = 0U";
static const char *census_union_use = "var _census_use: Variant = census_union";

static const CensusWitnessShape census_witness_shapes[] = {
	{ "none", "", "", true },
	{ "parser_data_type.container_element_types", census_container_witness, census_container_use, true },
	{ "parser_data_type.union_members", census_union_witness, census_union_use, true },
	{ "parser_data_type.type_arguments", census_arguments_witness, census_arguments_use, true },
	{ "parser_data_type.type_parameter_bound",
			"class CensusBounded[T: RefCounted] extends RefCounted:\n\tvar value: T",
			"var _census_use: Variant = CensusBounded.new()", true },
	{ "parser_data_type.method_parameter_types",
			"func census_parameters(first: uint, second: String) -> String:\n\treturn second + str(first)",
			"var _census_use: Variant = census_parameters(5U, \"tag\")", true },
	{ "parser_data_type.method_return_type", "func census_return() -> uint:\n\treturn 5U",
			"var _census_use: Variant = census_return()", true },
	{ "parser_data_type.method_rest_parameter_type",
			"func census_rest(...values: Array[uint]) -> int:\n\treturn values.size()",
			"var _census_use: Variant = census_rest(5U)", true },
	{ "parser_data_type.enum_case_payload_field_types",
			"enum CensusPayloadTypes:\n\tNumeric(amount: uint)\n\tEmpty",
			"var _census_use: Variant = CensusPayloadTypes.Numeric(5U)", true },
	{ "parser_data_type.enum_case_payload_field_names",
			"enum CensusPayloadNames:\n\tLabelled(label: String)\n\tEmpty",
			"var _census_use: Variant = CensusPayloadNames.Labelled(\"tag\")", true },
	{ "runtime_data_type.container_element_types", census_container_witness, census_container_use, true },
	{ "runtime_data_type.type_arguments", census_arguments_witness, census_arguments_use, true },
	{ "runtime_data_type.union_alternatives", census_union_witness, census_union_use, true },
	// The live container descriptor of a value, the serialized type record, and an instance's reified
	// specialization evidence are representations this adapter has no read path to from a compiled
	// script, so their child slots stay unrenderable rather than being claimed by a cell that would
	// observe nothing.
	{ "container_descriptor.element_types", census_container_witness, census_container_use, false },
	{ "container_descriptor.type_arguments", census_arguments_witness, census_arguments_use, false },
	{ "bytecode_serialized_type.serialized_container_element_types", census_container_witness,
			census_container_use, false },
	{ "bytecode_serialized_type.serialized_type_arguments", census_arguments_witness, census_arguments_use,
			false },
	{ "bytecode_serialized_type.serialized_union_alternatives", census_union_witness, census_union_use,
			false },
	{ "member_binding_descriptor.fixed",
			"class CensusOpen[T] extends RefCounted:\n\tvar value: T\n\n"
			"class CensusFixed extends CensusOpen[uint]:\n\tfunc fixed_value() -> Variant:\n\t\treturn value",
			"var _census_use: Variant = CensusFixed.new().fixed_value()", true },
	{ "member_binding_descriptor.tuple_slot_shape", "var census_tuple: (uint, String) = (5U, \"tag\")",
			"var _census_use: Variant = census_tuple", true },
	{ "reflection_property_info.hint_string", "@export var census_exported: Array[uint] = [5U]",
			"var _census_use: Variant = census_exported", true },
	{ "specialization_evidence.evidence_type_arguments", census_arguments_witness, census_arguments_use,
			false },
};

static const DestinationShape *find_destination_shape(const String &p_leaf) {
	for (const DestinationShape &shape : destination_shapes) {
		if (p_leaf == shape.leaf) {
			return &shape;
		}
	}
	return nullptr;
}

static const BoundaryShape *find_boundary_shape(const String &p_leaf) {
	for (const BoundaryShape &shape : boundary_shapes) {
		if (p_leaf == shape.leaf) {
			return &shape;
		}
	}
	return nullptr;
}

static const SourceProofShape *find_source_proof_shape(const String &p_leaf) {
	for (const SourceProofShape &shape : source_proof_shapes) {
		if (p_leaf == shape.leaf) {
			return &shape;
		}
	}
	return nullptr;
}

static const CensusWitnessShape *find_census_witness_shape(const String &p_leaf) {
	for (const CensusWitnessShape &shape : census_witness_shapes) {
		if (p_leaf == shape.leaf) {
			return shape.observed ? &shape : nullptr;
		}
	}
	return nullptr;
}

// A typed container enforces exactly one element type at run time, so `Array[uint | String]` is not a
// spellable slot. The pair is refused here rather than rendered into a program that cannot parse: a
// coordinate the adapter cannot realize is a catalog defect, not an observation.
static bool destination_crosses_boundary(const String &p_destination, const String &p_boundary) {
	return !(p_destination == "union" && p_boundary == "container_element_store");
}

static String source_expression_for(const String &p_source_proof) {
	const SourceProofShape *shape = find_source_proof_shape(p_source_proof);
	// Every caller validates the leaf through `can_render` first, so a missing shape is a defect in the
	// vocabulary tables rather than an input to fall back from.
	ERR_FAIL_NULL_V(shape, String());
	return shape->expression;
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

static String indent_block(const String &p_block) {
	const PackedStringArray lines = p_block.split("\n");
	String indented;
	for (int index = 0; index < lines.size(); index++) {
		indented += "\t" + lines[index] + "\n";
	}
	return indented;
}

// Declarations every wrapper-parity program carries, whatever its coordinates: the trait and class a
// nominal or trait destination is satisfied by, the generic carrier a reified argument lives in, the
// gradual and erasing sources, the callable a callable slot is filled with, and the carrier probe the
// program's output is read through.
static const char *wrapper_parity_preamble = R"FS(trait HasValue:
	var value: uint = 0U

class Carrier extends RefCounted:
	uses HasValue

class Box[T] extends RefCounted:
	var value: T

func supply(value):
	return value

func erase[T](value: T) -> Variant:
	return value

func identity_uint(value: uint) -> Variant:
	return value

func carrier_of(value: Variant) -> String:
	if value is uint:
		return "uint " + str(value)
	if value is int:
		return "int " + str(value)
	return "other"
)FS";

// The program one wrapper-parity cell observes. The value is carried through the boundary's own slot
// and read back out of it, and the descriptor the observation inspects is that same slot, so a
// boundary that lost its conversion or its check cannot be hidden by an intact declaration elsewhere.
static String render_wrapper_parity_source(const DestinationShape &p_destination,
		const BoundaryShape &p_boundary, const SourceProofShape &p_source_proof,
		const CensusWitnessShape &p_census_witness) {
	const String destination_type = p_destination.type_spelling;
	const String default_expression = p_destination.default_expression;
	auto substitute = [&](const String &p_text) {
		return p_text.replace("DESTINATION", destination_type).replace("DEFAULT", default_expression);
	};

	String source = wrapper_parity_preamble;
	const String declarations = substitute(p_boundary.declarations);
	if (!declarations.is_empty()) {
		source += "\n" + declarations + "\n";
	}
	const String census_declarations = p_census_witness.declarations;
	if (!census_declarations.is_empty()) {
		source += "\n" + census_declarations + "\n";
	}

	const String boundary_value =
			String(p_destination.boundary_value).replace("SOURCE", p_source_proof.wrapper_expression);
	String body = "var typed_source: uint = 5U\n"
				  "var variant_source: Variant = 5U\n"
				  "var inferred_source := 5U\n"
				  "var nested_source: Array = [5U]\n"
				  "var _source_witnesses: Variant = [typed_source, variant_source, inferred_source, "
				  "nested_source]\n";
	const String source_expression = p_source_proof.wrapper_expression;
	const String wrap_statements =
			String(p_destination.wrap_statements).replace("SOURCE", source_expression);
	if (!wrap_statements.is_empty()) {
		body += wrap_statements + "\n";
	}
	body += substitute(p_boundary.statements).replace("BOUNDARY_VALUE", boundary_value) + "\n";
	const String census_use = p_census_witness.use_statement;
	if (!census_use.is_empty()) {
		body += census_use + "\n";
	}
	const String unwrap_annotation = p_destination.unwrap_annotation;
	if (!unwrap_annotation.is_empty()) {
		body += unwrap_annotation + "\n";
	}
	body += vformat("var stored: Variant = %s\n",
			String(p_destination.unwrap_expression).replace("SOURCE", source_expression));
	body += "print(carrier_of(stored))";

	source += "\nfunc test() -> void:\n" + indent_block(body);
	return source;
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

// The strongest severity the observation recorded, decided exactly the way the runner's own severity
// profile decides it. Publishing it as a dimension lets a family require it, so an accepted cell that
// starts erroring - or a rejected one that decays into a warning - is a named mismatch rather than an
// unexplained diagnostics finding.
static String observed_diagnostic_severity(const FSCompletenessObservation &p_observation) {
	bool has_warning = false;
	for (int index = 0; index < p_observation.diagnostic_records.size(); index++) {
		const Dictionary record = p_observation.diagnostic_records[index];
		const String severity = record.get("severity", String());
		if (severity == DIAGNOSTIC_SEVERITY_ERROR) {
			return DIAGNOSTIC_SEVERITY_ERROR;
		}
		has_warning = has_warning || severity == DIAGNOSTIC_SEVERITY_WARNING;
	}
	return has_warning ? DIAGNOSTIC_SEVERITY_WARNING : "none";
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

// The coordinates one program was rendered from. A cell that names a census child slot lives in the
// wrapper-parity coordinate space and carries five axes; the destination-membership pilot's space
// carries four and no census child. Which space a cell belongs to decides the program layout, so it
// is read once here and passed on rather than re-derived at each use.
struct ProgramCoordinates {
	String destination;
	String source_proof;
	String boundary;
	String surface;
	String census_child;
	bool names_census_child = false;
};

static bool read_coordinates(const Dictionary &p_coordinates, ProgramCoordinates &r_read) {
	r_read = ProgramCoordinates();
	const bool names_census_child = p_coordinates.has("census_child");
	if (p_coordinates.size() != (names_census_child ? 5 : 4)) {
		return false;
	}
	if (!read_coordinate(p_coordinates, "destination", r_read.destination) ||
			!read_coordinate(p_coordinates, "source_proof", r_read.source_proof) ||
			!read_coordinate(p_coordinates, "boundary", r_read.boundary) ||
			!read_coordinate(p_coordinates, "surface", r_read.surface)) {
		return false;
	}
	r_read.names_census_child = names_census_child;
	r_read.census_child = "none";
	if (names_census_child && !read_coordinate(p_coordinates, "census_child", r_read.census_child)) {
		return false;
	}
	return destination_crosses_boundary(r_read.destination, r_read.boundary);
}

static bool read_program_coordinates(const FSCompletenessProgram &p_program, String &r_destination,
		String &r_source_proof, String &r_boundary, String &r_surface) {
	ProgramCoordinates coordinates;
	if (!read_coordinates(p_program.coordinates, coordinates)) {
		return false;
	}
	r_destination = coordinates.destination;
	r_source_proof = coordinates.source_proof;
	r_boundary = coordinates.boundary;
	r_surface = coordinates.surface;
	return true;
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
class DestinationWrapperBytecodeResolver : public FSBytecodeExternalResolver {
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
	DestinationWrapperInternal::SyntheticSourceScope synthetic_source(
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
		DestinationWrapperBytecodeResolver resolver;
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

static bool is_builtin_of(const FSDataType &p_descriptor, Variant::Type p_builtin_type) {
	return p_descriptor.kind == FSDataType::BUILTIN && p_descriptor.builtin_type == p_builtin_type;
}

static bool is_script_kind(const FSDataType &p_descriptor) {
	return p_descriptor.kind == FSDataType::FOUNDRY_SCRIPT || p_descriptor.kind == FSDataType::SCRIPT;
}

// Whether the descriptor the runtime kept for the destination slot is the shape the coordinates
// declare. The structure is checked rather than a rendered spelling, so a destination that erased its
// wrapper at run time is caught even when it still prints the same name.
static bool descriptor_is_expected_destination(const FSDataType &p_descriptor, const String &p_destination) {
	if (p_destination == "plain") {
		return is_builtin_of(p_descriptor, Variant::UINT) && !p_descriptor.is_nullable;
	}
	if (p_destination == "optional") {
		return is_builtin_of(p_descriptor, Variant::UINT) && p_descriptor.is_nullable;
	}
	if (p_destination == "container_element") {
		return is_builtin_of(p_descriptor, Variant::ARRAY) &&
				p_descriptor.container_element_types.size() == 1 &&
				is_builtin_of(p_descriptor.container_element_types[0], Variant::UINT);
	}
	if (p_destination == "generic_argument") {
		return is_script_kind(p_descriptor) && !p_descriptor.is_script_trait &&
				p_descriptor.type_arguments.size() == 1 &&
				is_builtin_of(p_descriptor.type_arguments[0], Variant::UINT);
	}
	if (p_destination == "tuple_field") {
		if (p_descriptor.kind == FSDataType::TUPLE) {
			return p_descriptor.container_element_types.size() == 2 &&
					is_builtin_of(p_descriptor.container_element_types[0], Variant::UINT) &&
					is_builtin_of(p_descriptor.container_element_types[1], Variant::STRING);
		}
		// A tuple slot lowers to the read-only Array its values are carried in, with no element type:
		// the shape survives only in `is` tests. An element-typed Array is a container destination and
		// stays distinguishable from it.
		return is_builtin_of(p_descriptor, Variant::ARRAY) && p_descriptor.container_element_types.is_empty();
	}
	if (p_destination == "callable_slot") {
		return is_builtin_of(p_descriptor, Variant::CALLABLE);
	}
	if (p_destination == "nominal_class") {
		return is_script_kind(p_descriptor) && !p_descriptor.is_script_trait &&
				p_descriptor.type_arguments.is_empty();
	}
	if (p_destination == "trait") {
		return p_descriptor.get_source_type_name() == "HasValue";
	}
	if (p_descriptor.kind != FSDataType::UNION || p_descriptor.union_alternatives.size() != 2) {
		return false;
	}
	bool has_uint = false;
	bool has_string = false;
	for (const FSDataType &alternative : p_descriptor.union_alternatives) {
		has_uint = has_uint || is_builtin_of(alternative, Variant::UINT);
		has_string = has_string || is_builtin_of(alternative, Variant::STRING);
	}
	return has_uint && has_string;
}

struct RuntimeDescriptorEvidence {
	FSDataType descriptor;
	// False when the site is a local, whose descriptor exists only in the analyzer's record.
	bool has_runtime_descriptor = false;
	// `stored_carrier` outcome the observed destination realizes, empty when it realizes none.
	String carrier;
	bool site_is_union = false;
	ObjectID inspected_instance_id;
	ObjectID inspected_owner_instance_id;
	bool inspected_compiled_binary = false;
};

static FSParser::FunctionNode *parser_member_function(FSParser::ClassNode *p_tree, const StringName &p_member);

// Which `stored_carrier` outcome a runtime destination descriptor realizes. Structural rather than
// nominal: a destination that erased its wrapper reads as a different carrier here even when the
// program still spells the same type.
static String runtime_destination_carrier(const FSDataType &p_descriptor) {
	if (p_descriptor.kind == FSDataType::UNION) {
		bool has_uint = false;
		bool has_string = false;
		for (const FSDataType &alternative : p_descriptor.union_alternatives) {
			has_uint = has_uint || is_builtin_of(alternative, Variant::UINT);
			has_string = has_string || is_builtin_of(alternative, Variant::STRING);
		}
		return has_uint && has_string ? "admitting_alternative" : String();
	}
	if (is_builtin_of(p_descriptor, Variant::UINT)) {
		return p_descriptor.is_nullable ? "optional_payload" : "plain_destination";
	}
	if (p_descriptor.kind == FSDataType::TUPLE) {
		return p_descriptor.container_element_types.size() == 2 &&
						is_builtin_of(p_descriptor.container_element_types[0], Variant::UINT) &&
						is_builtin_of(p_descriptor.container_element_types[1], Variant::STRING)
				? "tuple_slot"
				: String();
	}
	if (is_builtin_of(p_descriptor, Variant::ARRAY)) {
		if (p_descriptor.container_element_types.size() == 1 &&
				is_builtin_of(p_descriptor.container_element_types[0], Variant::UINT)) {
			return "element_slot";
		}
		// A tuple slot lowers to the read-only Array its values are carried in, with no element type.
		return p_descriptor.container_element_types.is_empty() ? "tuple_slot" : String();
	}
	if (is_builtin_of(p_descriptor, Variant::CALLABLE)) {
		return "callable_slot";
	}
	if (p_descriptor.get_source_type_name() == "HasValue") {
		return "trait_witness";
	}
	if (is_script_kind(p_descriptor)) {
		if (p_descriptor.type_arguments.is_empty()) {
			return "nominal_instance";
		}
		return p_descriptor.type_arguments.size() == 1 &&
						is_builtin_of(p_descriptor.type_arguments[0], Variant::UINT)
				? "reified_argument"
				: String();
	}
	return String();
}

// The same question asked of the analyzer's record, which is the only representation that keeps a
// typed local. Spelling rather than structure, because `FSParser::DataType::to_string()` is the
// analyzer's own rendering of the slot and changes with any part of it the destination depends on.
static String parser_destination_carrier(const FSParser::DataType &p_datatype) {
	const String spelling = p_datatype.to_string();
	if (spelling == "uint") {
		return "plain_destination";
	}
	if (spelling == "String | uint") {
		return "admitting_alternative";
	}
	if (spelling == "uint?") {
		return "optional_payload";
	}
	if (spelling == "Array[uint]") {
		return "element_slot";
	}
	if (spelling == "Box[uint]") {
		return "reified_argument";
	}
	if (spelling == "(uint, String)") {
		return "tuple_slot";
	}
	if (spelling.begins_with("Callable[")) {
		return "callable_slot";
	}
	if (spelling == "Carrier") {
		return "nominal_instance";
	}
	if (spelling == "HasValue") {
		return "trait_witness";
	}
	return String();
}

// The destination descriptor of the boundary the coordinates name, read from the slot that boundary
// writes through. A local site is read from a fresh analysis of the same program, because a compiled
// script keeps no record of a local.
static RuntimeInspectionStepResult inspect_boundary_destination(const FSCompletenessProgram &p_program,
		const Ref<FoundryScript> &p_inspected, const BoundaryShape &p_boundary,
		RuntimeDescriptorEvidence &r_evidence, PackedStringArray &r_diagnostics) {
	if (p_boundary.site == SITE_FUNCTION_LOCAL) {
		DestinationWrapperInternal::SyntheticSourceScope synthetic_source(
				"boundary_local_" + p_program.case_id, p_program.source);
		if (!synthetic_source.is_available()) {
			r_diagnostics.push_back("Analyzer identity for the local destination site is unavailable.");
			return runtime_inspection_failure(ERR_CANT_CREATE, RUNTIME_INSPECTION_STRUCTURAL_FAILURE);
		}
		FSParser parser;
		if (parser.parse(p_program.source, synthetic_source.get_path(), false) != OK) {
			r_diagnostics.push_back("The local destination site could not be parsed.");
			return runtime_inspection_failure(ERR_PARSE_ERROR, RUNTIME_INSPECTION_PRODUCT_FAILURE);
		}
		FSAnalyzer analyzer(&parser);
		if (analyzer.analyze() != OK) {
			r_diagnostics.push_back("The local destination site could not be analyzed.");
			return runtime_inspection_failure(ERR_PARSE_ERROR, RUNTIME_INSPECTION_PRODUCT_FAILURE);
		}
		FSParser::ClassNode *tree = parser.get_tree();
		FSParser::FunctionNode *function =
				tree == nullptr ? nullptr : parser_member_function(tree, StringName(p_boundary.site_owner));
		if (function == nullptr || function->body == nullptr ||
				!function->body->has_local(StringName(p_boundary.site_name))) {
			r_diagnostics.push_back(vformat("Local destination '%s.%s' is unavailable.",
					p_boundary.site_owner, p_boundary.site_name));
			return runtime_inspection_failure(ERR_INVALID_DATA, RUNTIME_INSPECTION_PRODUCT_FAILURE);
		}
		r_evidence.carrier = parser_destination_carrier(
				function->body->get_local(StringName(p_boundary.site_name)).get_datatype());
		r_evidence.site_is_union = r_evidence.carrier == "admitting_alternative";
		r_evidence.has_runtime_descriptor = false;
		return RuntimeInspectionStepResult();
	}

	if (p_inspected.is_null()) {
		r_diagnostics.push_back("Compiled runtime script is unavailable for descriptor inspection.");
		return runtime_inspection_failure(ERR_INVALID_DATA, RUNTIME_INSPECTION_STRUCTURAL_FAILURE);
	}
	if (p_boundary.site == SITE_FUNCTION_ARGUMENT || p_boundary.site == SITE_FUNCTION_RETURN) {
		FSFunction *const *function =
				p_inspected->get_member_functions().getptr(StringName(p_boundary.site_owner));
		if (function == nullptr || *function == nullptr) {
			r_diagnostics.push_back(vformat("Compiled function '%s' is unavailable.", p_boundary.site_owner));
			return runtime_inspection_failure(ERR_INVALID_DATA, RUNTIME_INSPECTION_PRODUCT_FAILURE);
		}
		if (p_boundary.site == SITE_FUNCTION_RETURN) {
			r_evidence.descriptor = (*function)->get_return_type();
		} else {
			if ((*function)->get_argument_count() != 1) {
				r_diagnostics.push_back(vformat(
						"Compiled function '%s' does not expose one destination argument.", p_boundary.site_owner));
				return runtime_inspection_failure(ERR_INVALID_DATA, RUNTIME_INSPECTION_PRODUCT_FAILURE);
			}
			r_evidence.descriptor = (*function)->get_argument_type(0);
		}
		r_evidence.has_runtime_descriptor = true;
		r_evidence.carrier = runtime_destination_carrier(r_evidence.descriptor);
		r_evidence.site_is_union = r_evidence.descriptor.kind == FSDataType::UNION;
		return RuntimeInspectionStepResult();
	}

	const Ref<FoundryScript> *owner =
			p_inspected->get_subclasses().getptr(StringName(p_boundary.site_owner));
	if (owner == nullptr || owner->is_null()) {
		r_diagnostics.push_back(vformat("Compiled class '%s' is unavailable.", p_boundary.site_owner));
		return runtime_inspection_failure(ERR_INVALID_DATA, RUNTIME_INSPECTION_PRODUCT_FAILURE);
	}
	r_evidence.inspected_owner_instance_id = (*owner)->get_instance_id();
	const FSDataType *member = (*owner)->find_member_data_type(StringName(p_boundary.site_name));
	if (member == nullptr) {
		r_diagnostics.push_back(vformat("Compiled descriptor '%s.%s' is unavailable.", p_boundary.site_owner,
				p_boundary.site_name));
		return runtime_inspection_failure(ERR_INVALID_DATA, RUNTIME_INSPECTION_PRODUCT_FAILURE);
	}
	if (p_boundary.site == SITE_CLASS_MEMBER_ELEMENT) {
		if (member->container_element_types.size() != 1) {
			r_diagnostics.push_back(vformat("Compiled container '%s.%s' declares no single element type.",
					p_boundary.site_owner, p_boundary.site_name));
			return runtime_inspection_failure(ERR_INVALID_DATA, RUNTIME_INSPECTION_PRODUCT_FAILURE);
		}
		r_evidence.descriptor = member->container_element_types[0];
	} else {
		r_evidence.descriptor = *member;
	}
	r_evidence.has_runtime_descriptor = true;
	r_evidence.carrier = runtime_destination_carrier(r_evidence.descriptor);
	r_evidence.site_is_union = r_evidence.descriptor.kind == FSDataType::UNION;
	return RuntimeInspectionStepResult();
}

static RuntimeInspectionStepResult inspect_runtime_destination_descriptor(const Ref<FoundryScript> &p_inspected,
		const ProgramCoordinates &p_coordinates, RuntimeDescriptorEvidence &r_evidence,
		PackedStringArray &r_diagnostics) {
	r_evidence = RuntimeDescriptorEvidence();
	if (p_inspected.is_null()) {
		r_diagnostics.push_back("Compiled runtime script is unavailable for descriptor inspection.");
		return runtime_inspection_failure(ERR_INVALID_DATA, RUNTIME_INSPECTION_STRUCTURAL_FAILURE);
	}
	r_evidence.inspected_instance_id = p_inspected->get_instance_id();
	r_evidence.inspected_compiled_binary = p_inspected->is_compiled_binary();
	if (p_coordinates.boundary == "argument_binding") {
		FSFunction *const *accept = p_inspected->get_member_functions().getptr(SNAME("accept"));
		if (accept == nullptr || *accept == nullptr || (*accept)->get_argument_count() != 1) {
			r_diagnostics.push_back("Compiled accept function does not expose one destination argument.");
			return runtime_inspection_failure(ERR_INVALID_DATA, RUNTIME_INSPECTION_PRODUCT_FAILURE);
		}
		r_evidence.descriptor = (*accept)->get_argument_type(0);
		r_evidence.has_runtime_descriptor = true;
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
	r_evidence.has_runtime_descriptor = true;
	return RuntimeInspectionStepResult();
}

// Folds a type spelling into a stable outcome token. An observation has to be comparable against the
// outcome a manifest declares, and a spelling carries characters an outcome id may not.
static String census_evidence_token(const String &p_prefix, const String &p_spelling) {
	String folded;
	bool pending_separator = false;
	for (int index = 0; index < p_spelling.length(); index++) {
		const char32_t character = p_spelling[index];
		const bool alphanumeric = (character >= 'a' && character <= 'z') ||
				(character >= 'A' && character <= 'Z') || (character >= '0' && character <= '9');
		if (!alphanumeric) {
			pending_separator = !folded.is_empty();
			continue;
		}
		if (pending_separator) {
			folded += "_";
			pending_separator = false;
		}
		folded += String::chr(character >= 'A' && character <= 'Z' ? character + 32 : character);
	}
	return folded.is_empty() ? p_prefix + "_absent" : p_prefix + "_" + folded;
}

static String joined_parser_spelling(const Vector<FSParser::DataType> &p_types) {
	String spelling;
	for (int index = 0; index < p_types.size(); index++) {
		spelling += (index == 0 ? "" : " ") + p_types[index].to_string();
	}
	return spelling;
}

static String joined_runtime_spelling(const Vector<FSDataType> &p_types) {
	String spelling;
	for (int index = 0; index < p_types.size(); index++) {
		spelling += (index == 0 ? "" : " ") + p_types[index].get_source_type_name();
	}
	return spelling;
}

static const FSParser::DataType *parser_member_type(
		FSParser::ClassNode *p_tree, const StringName &p_member, FSParser::DataType &r_storage) {
	if (p_tree == nullptr || !p_tree->has_member(p_member)) {
		return nullptr;
	}
	const FSParser::ClassNode::Member member = p_tree->get_member(p_member);
	if (member.type != FSParser::ClassNode::Member::VARIABLE || member.variable == nullptr) {
		return nullptr;
	}
	r_storage = member.variable->get_datatype();
	return &r_storage;
}

static FSParser::FunctionNode *parser_member_function(FSParser::ClassNode *p_tree, const StringName &p_member) {
	if (p_tree == nullptr || !p_tree->has_member(p_member)) {
		return nullptr;
	}
	const FSParser::ClassNode::Member member = p_tree->get_member(p_member);
	return member.type == FSParser::ClassNode::Member::FUNCTION ? member.function : nullptr;
}

static FSParser::EnumNode *parser_member_enum(FSParser::ClassNode *p_tree, const StringName &p_member) {
	if (p_tree == nullptr || !p_tree->has_member(p_member)) {
		return nullptr;
	}
	const FSParser::ClassNode::Member member = p_tree->get_member(p_member);
	return member.type == FSParser::ClassNode::Member::ENUM ? member.m_enum : nullptr;
}

// The named child slot read out of the analyzer's own resolved record. Returns an empty string when
// the slot cannot be read, which the caller reports rather than silently passing.
static String parser_census_evidence(FSParser::ClassNode *p_tree, const String &p_census_child) {
	FSParser::DataType storage;
	if (p_census_child == "parser_data_type.container_element_types") {
		const FSParser::DataType *type = parser_member_type(p_tree, SNAME("census_container"), storage);
		return type == nullptr ? String()
							   : census_evidence_token("parser_element",
										 joined_parser_spelling(type->container_element_types));
	}
	if (p_census_child == "parser_data_type.union_members") {
		const FSParser::DataType *type = parser_member_type(p_tree, SNAME("census_union"), storage);
		return type == nullptr
				? String()
				: census_evidence_token("parser_union", joined_parser_spelling(type->union_members));
	}
	if (p_census_child == "parser_data_type.type_arguments") {
		const FSParser::DataType *type = parser_member_type(p_tree, SNAME("census_arguments"), storage);
		return type == nullptr
				? String()
				: census_evidence_token("parser_argument", joined_parser_spelling(type->type_arguments));
	}
	if (p_census_child == "parser_data_type.type_parameter_bound") {
		if (p_tree == nullptr || !p_tree->has_member(SNAME("CensusBounded"))) {
			return String();
		}
		const FSParser::ClassNode::Member member = p_tree->get_member(SNAME("CensusBounded"));
		if (member.type != FSParser::ClassNode::Member::CLASS || member.m_class == nullptr ||
				member.m_class->type_parameters.is_empty() || member.m_class->type_parameters[0] == nullptr) {
			return String();
		}
		return census_evidence_token(
				"parser_bound", member.m_class->type_parameters[0]->resolved_bound.to_string());
	}
	if (p_census_child == "parser_data_type.method_parameter_types") {
		FSParser::FunctionNode *function = parser_member_function(p_tree, SNAME("census_parameters"));
		if (function == nullptr) {
			return String();
		}
		Vector<FSParser::DataType> parameters;
		for (FSParser::ParameterNode *parameter : function->parameters) {
			if (parameter == nullptr) {
				return String();
			}
			parameters.push_back(parameter->get_datatype());
		}
		return census_evidence_token("parser_parameters", joined_parser_spelling(parameters));
	}
	if (p_census_child == "parser_data_type.method_return_type") {
		FSParser::FunctionNode *function = parser_member_function(p_tree, SNAME("census_return"));
		return function == nullptr
				? String()
				: census_evidence_token("parser_return", function->get_datatype().to_string());
	}
	if (p_census_child == "parser_data_type.method_rest_parameter_type") {
		FSParser::FunctionNode *function = parser_member_function(p_tree, SNAME("census_rest"));
		if (function == nullptr || function->rest_parameter == nullptr) {
			return String();
		}
		return census_evidence_token("parser_rest", function->rest_parameter->get_datatype().to_string());
	}
	if (p_census_child == "parser_data_type.enum_case_payload_field_types" ||
			p_census_child == "parser_data_type.enum_case_payload_field_names") {
		const bool names = p_census_child.ends_with("field_names");
		FSParser::EnumNode *enumeration =
				parser_member_enum(p_tree, names ? SNAME("CensusPayloadNames") : SNAME("CensusPayloadTypes"));
		if (enumeration == nullptr) {
			return String();
		}
		String spelling;
		for (const FSParser::EnumNode::Value &value : enumeration->values) {
			for (const FSParser::EnumNode::PayloadField &field : value.payload_fields) {
				if (names) {
					if (field.identifier == nullptr) {
						return String();
					}
					spelling += String(field.identifier->name) + " ";
				} else {
					if (field.type == nullptr) {
						return String();
					}
					spelling += field.type->get_datatype().to_string() + " ";
				}
			}
		}
		return spelling.is_empty()
				? String()
				: census_evidence_token(names ? "parser_payload_names" : "parser_payload_types", spelling);
	}
	return String();
}

// The named child slot read out of the compiled script: the runtime type record, the member binding
// descriptor, or the reflected property info, whichever representation the coordinate names.
static String runtime_census_evidence(const Ref<FoundryScript> &p_inspected, const String &p_census_child) {
	if (p_inspected.is_null()) {
		return String();
	}
	if (p_census_child == "runtime_data_type.container_element_types") {
		const FSDataType *type = p_inspected->find_member_data_type(SNAME("census_container"));
		return type == nullptr ? String()
							   : census_evidence_token("runtime_element",
										 joined_runtime_spelling(type->container_element_types));
	}
	if (p_census_child == "runtime_data_type.type_arguments") {
		const FSDataType *type = p_inspected->find_member_data_type(SNAME("census_arguments"));
		return type == nullptr
				? String()
				: census_evidence_token("runtime_argument", joined_runtime_spelling(type->type_arguments));
	}
	if (p_census_child == "runtime_data_type.union_alternatives") {
		const FSDataType *type = p_inspected->find_member_data_type(SNAME("census_union"));
		return type == nullptr ? String()
							   : census_evidence_token("runtime_union",
										 joined_runtime_spelling(type->union_alternatives));
	}
	if (p_census_child == "member_binding_descriptor.fixed") {
		const Ref<FoundryScript> *fixed = p_inspected->get_subclasses().getptr(SNAME("CensusFixed"));
		if (fixed == nullptr || fixed->is_null()) {
			return String();
		}
		const auto *member = (*fixed)->debug_get_member_indices().getptr(SNAME("value"));
		if (member == nullptr) {
			return String();
		}
		return census_evidence_token("binding_fixed",
				vformat("%d %s", int(member->type_argument_binding.kind),
						member->type_argument_binding.fixed.get_source_type_name()));
	}
	if (p_census_child == "member_binding_descriptor.tuple_slot_shape") {
		const auto *member = p_inspected->debug_get_member_indices().getptr(SNAME("census_tuple"));
		if (member == nullptr) {
			return String();
		}
		return census_evidence_token(
				"binding_tuple", joined_runtime_spelling(member->tuple_slot_shape.container_element_types));
	}
	if (p_census_child == "reflection_property_info.hint_string") {
		// Read through the script's own reflection surface rather than off the type record: the hint
		// string is what an inspector, an export, and a tool see, and it is produced by a different
		// path than the descriptor the store consults.
		List<PropertyInfo> properties;
		p_inspected->get_script_property_list(&properties);
		for (const PropertyInfo &property : properties) {
			if (property.name != SNAME("census_exported")) {
				continue;
			}
			// Test seam: proves the cell is decided by the observed hint rather than by the
			// coordinates, by producing exactly what a regression that stops populating it would.
			const String hint_string = blank_reflection_hint_for_test ? String() : property.hint_string;
			return census_evidence_token("reflection_hint",
					vformat("%d %s", blank_reflection_hint_for_test ? 0 : int(property.hint), hint_string));
		}
		return String();
	}
	return String();
}

// The named census child slot, read from the representation the leaf names. `none` is the one leaf
// with nothing to read, and it is still an observation: a program that grew a census declaration it
// was not supposed to have would stop reporting it.
static String observe_census_child(const FSCompletenessProgram &p_program,
		const Ref<FoundryScript> &p_inspected, const String &p_census_child) {
	if (p_census_child == "none") {
		return "absent";
	}
	if (!p_census_child.begins_with("parser_data_type.")) {
		return runtime_census_evidence(p_inspected, p_census_child);
	}
	DestinationWrapperInternal::SyntheticSourceScope synthetic_source(
			"census_child_" + p_program.case_id, p_program.source);
	if (!synthetic_source.is_available()) {
		return String();
	}
	FSParser parser;
	if (parser.parse(p_program.source, synthetic_source.get_path(), false) != OK) {
		return String();
	}
	FSAnalyzer analyzer(&parser);
	if (analyzer.analyze() != OK) {
		return String();
	}
	return parser_census_evidence(parser.get_tree(), p_census_child);
}

static String extract_program_output(const FSTestRunner::FixtureOutcome &p_outcome) {
	const String status_line = "FS_TEST_OK\n";
	if (p_outcome.output.begins_with(status_line)) {
		return p_outcome.output.trim_prefix(status_line);
	}
	return p_outcome.output;
}

} // namespace

void DestinationWrapperInternal::set_persisted_write_test_hook(PersistedWriteTestHook p_hook) {
	persisted_write_test_hook = p_hook;
}

void DestinationWrapperInternal::set_blank_reflection_hint_for_test(bool p_blank) {
	blank_reflection_hint_for_test = p_blank;
}

DestinationWrapperInternal::SyntheticSourceScope::SyntheticSourceScope(
		const String &p_identity, const String &p_source) :
		lock(synthetic_source_mutex()) {
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

DestinationWrapperInternal::SyntheticSourceScope::~SyntheticSourceScope() {
	if (!path.is_empty()) {
		FSCache::remove_parser(path);
		FSCache::remove_script(path);
		FSCache::clear_source_override(path);
	}
	if (tree != nullptr) {
		memdelete(tree);
	}
}

Vector<String> destination_wrapper_destinations() {
	Vector<String> leaves;
	for (const DestinationShape &shape : destination_shapes) {
		leaves.push_back(shape.leaf);
	}
	return leaves;
}

Vector<String> destination_wrapper_boundaries() {
	Vector<String> leaves;
	for (const BoundaryShape &shape : boundary_shapes) {
		leaves.push_back(shape.leaf);
	}
	return leaves;
}

Vector<String> destination_wrapper_source_proofs() {
	Vector<String> leaves;
	for (const SourceProofShape &shape : source_proof_shapes) {
		leaves.push_back(shape.leaf);
	}
	return leaves;
}

Vector<String> destination_wrapper_census_children() {
	Vector<String> leaves;
	for (const CensusWitnessShape &shape : census_witness_shapes) {
		if (shape.observed) {
			leaves.push_back(shape.leaf);
		}
	}
	return leaves;
}

Vector<String> destination_wrapper_unobserved_census_children() {
	Vector<String> leaves;
	for (const CensusWitnessShape &shape : census_witness_shapes) {
		if (!shape.observed) {
			leaves.push_back(shape.leaf);
		}
	}
	return leaves;
}

const FSDestinationWrapperAdapter &FSDestinationWrapperAdapter::shared() {
	static FSDestinationWrapperAdapter adapter;
	return adapter;
}

String FSDestinationWrapperAdapter::id() const {
	return destination_wrapper_adapter_id;
}

Vector<String> FSDestinationWrapperAdapter::families() {
	Vector<String> family_ids;
	family_ids.push_back("union_destination_membership");
	for (const String &boundary : destination_wrapper_boundaries()) {
		family_ids.push_back("wrapper_parity_" + boundary);
	}
	family_ids.sort();
	return family_ids;
}

HashSet<String> FSDestinationWrapperAdapter::observable_dimensions() const {
	return HashSet<String>({ "analysis", "runtime_obligation", "stored_carrier", "diagnostic_severity",
			"census_child_evidence" });
}

HashMap<String, Vector<String>> FSDestinationWrapperAdapter::renderable_leaves() const {
	HashMap<String, Vector<String>> leaves;
	leaves["destination"] = destination_wrapper_destinations();
	leaves["source_proof"] = destination_wrapper_source_proofs();
	leaves["boundary"] = destination_wrapper_boundaries();
	leaves["census_child"] = destination_wrapper_census_children();
	leaves["surface"] = Vector<String>({ "text", "bytecode" });
	return leaves;
}

Error FSDestinationWrapperAdapter::render(
		const FSCompletenessResolvedCell &p_cell, FSCompletenessProgram &r_program) const {
	r_program = FSCompletenessProgram();
	ProgramCoordinates coordinates;
	if (p_cell.case_id.is_empty() || !read_coordinates(p_cell.coordinates, coordinates)) {
		return ERR_INVALID_DATA;
	}
	const String &destination = coordinates.destination;
	const String &source_proof = coordinates.source_proof;
	const String &boundary = coordinates.boundary;
	const String &surface = coordinates.surface;

	if (coordinates.names_census_child) {
		const DestinationShape *destination_shape = find_destination_shape(destination);
		const BoundaryShape *boundary_shape = find_boundary_shape(boundary);
		const SourceProofShape *source_proof_shape = find_source_proof_shape(source_proof);
		const CensusWitnessShape *census_witness_shape = find_census_witness_shape(coordinates.census_child);
		if (destination_shape == nullptr || boundary_shape == nullptr || source_proof_shape == nullptr ||
				census_witness_shape == nullptr) {
			return ERR_INVALID_DATA;
		}
		FSCompletenessProgram wrapper_program;
		wrapper_program.case_id = p_cell.case_id;
		wrapper_program.surface = surface;
		wrapper_program.coordinates = p_cell.coordinates.duplicate();
		wrapper_program.source = render_wrapper_parity_source(
				*destination_shape, *boundary_shape, *source_proof_shape, *census_witness_shape);
		wrapper_program.expected_output = "uint 5\n";
		r_program = wrapper_program;
		return OK;
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

FSCompletenessObservation FSDestinationWrapperAdapter::analyze(
		const FSCompletenessProgram &p_program, const String &p_surface) const {
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
	DestinationWrapperInternal::SyntheticSourceScope synthetic_source(p_program.case_id, p_program.source);
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
	observation.dimensions["diagnostic_severity"] = observed_diagnostic_severity(observation);
	return observation;
}

static FSCompletenessObservation inspect_runtime_contract_body(
		const FSCompletenessProgram &p_program, const Dictionary &p_runtime_context, Error &r_structural_error) {
	r_structural_error = OK;
	FSCompletenessObservation observation = FSDestinationWrapperAdapter::shared().analyze(p_program, p_program.surface);

	ProgramCoordinates coordinates;
	if (!read_coordinates(p_program.coordinates, coordinates) ||
			coordinates.surface != p_program.surface) {
		observation.diagnostics.push_back("Runtime contract coordinates are invalid.");
		r_structural_error = ERR_INVALID_DATA;
		return observation;
	}
	const String &destination = coordinates.destination;
	const String &source_proof = coordinates.source_proof;
	const String &boundary = coordinates.boundary;
	const String &surface = coordinates.surface;
	for (const String &axis : Completeness::sorted_dictionary_keys(p_program.coordinates)) {
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
	const BoundaryShape *boundary_shape = find_boundary_shape(boundary);
	if (coordinates.names_census_child && boundary_shape == nullptr) {
		observation.diagnostics.push_back("Runtime contract coordinates name no rendered boundary.");
		r_structural_error = ERR_INVALID_DATA;
		return observation;
	}
	// A wrapper-parity cell reads the descriptor of the slot its own boundary wrote through; the
	// destination-membership pilot keeps the sites its programs declare.
	const RuntimeInspectionStepResult descriptor_result = coordinates.names_census_child
			? inspect_boundary_destination(
					  p_program, inspected, *boundary_shape, descriptor_evidence, observation.diagnostics)
			: inspect_runtime_destination_descriptor(
					  inspected, coordinates, descriptor_evidence, observation.diagnostics);
	observation.dimensions["original_instance_id"] = int64_t(original_instance_id);
	if (descriptor_evidence.inspected_instance_id.is_valid()) {
		observation.dimensions["inspected_instance_id"] = int64_t(descriptor_evidence.inspected_instance_id);
	}
	observation.dimensions["inspected_compiled_binary"] = descriptor_evidence.inspected_compiled_binary;
	if ((coordinates.names_census_child || boundary == "reflective_write") &&
			descriptor_evidence.inspected_owner_instance_id.is_valid()) {
		observation.dimensions["inspected_owner_instance_id"] =
				int64_t(descriptor_evidence.inspected_owner_instance_id);
	}
	if (descriptor_result.disposition != RUNTIME_INSPECTION_COMPLETE) {
		if (descriptor_result.disposition == RUNTIME_INSPECTION_STRUCTURAL_FAILURE) {
			r_structural_error = descriptor_result.error;
		}
		return observation;
	}

	if (!coordinates.names_census_child &&
			!descriptor_is_expected_destination(descriptor_evidence.descriptor, destination)) {
		observation.diagnostics.push_back(vformat(
				"Runtime destination descriptor '%s' does not match declared '%s' coordinates.",
				descriptor_evidence.descriptor.get_source_type_name(), destination));
	}
	if (coordinates.names_census_child && descriptor_evidence.carrier.is_empty()) {
		observation.diagnostics.push_back(vformat(
				"The '%s' boundary's destination realizes no carrier this family can name.", boundary));
	}
	if (descriptor_evidence.has_runtime_descriptor) {
		Ref<RefCounted> unrelated;
		unrelated.instantiate();
		const Variant unrelated_value = unrelated;
		if (descriptor_evidence.descriptor.is_type(unrelated_value)) {
			observation.diagnostics.push_back("Runtime destination descriptor admits RefCounted.new().");
		}
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

	const SourceProofShape *source_proof_shape = find_source_proof_shape(source_proof);
	const DestinationShape *destination_shape = find_destination_shape(destination);
	if (source_proof_shape == nullptr || destination_shape == nullptr) {
		observation.diagnostics.push_back("Runtime contract coordinates name no rendered shape.");
		r_structural_error = ERR_INVALID_DATA;
		return observation;
	}
	if (coordinates.names_census_child) {
		// Both dimensions are read back from the boundary's own destination rather than derived from
		// the coordinates: a boundary that lost its wrapper reports a different carrier here, and a
		// destination that stopped being a set reports a different obligation.
		observation.dimensions["stored_carrier"] = descriptor_evidence.carrier;
		if (source_proof_shape->unproven) {
			observation.dimensions["runtime_obligation"] = descriptor_evidence.site_is_union ||
							descriptor_evidence.carrier == "admitting_alternative"
					? "union_membership_check"
					: "typed_destination_check";
		}
	} else {
		if (source_proof_shape->unproven) {
			observation.dimensions["runtime_obligation"] =
					destination == "union" ? "union_membership_check" : "typed_destination_check";
		}
		if (source_proof_shape->carrier_provable) {
			observation.dimensions["stored_carrier"] = destination_shape->stored_carrier;
		}
	}

	if (coordinates.names_census_child) {
		const String evidence = observe_census_child(p_program, inspected, coordinates.census_child);
		if (evidence.is_empty()) {
			observation.diagnostics.push_back(vformat(
					"Census child '%s' could not be read from the representation it names.",
					coordinates.census_child));
			return observation;
		}
		observation.dimensions["census_child_evidence"] = evidence;
	}
	return observation;
}

static FSCompletenessObservation inspect_runtime_contract_internal(
		const FSCompletenessProgram &p_program, const Dictionary &p_runtime_context, Error &r_structural_error) {
	FSCompletenessObservation observation =
			inspect_runtime_contract_body(p_program, p_runtime_context, r_structural_error);
	cover_diagnostics_with_records(observation);
	observation.dimensions["diagnostic_severity"] = observed_diagnostic_severity(observation);
	return observation;
}

FSCompletenessObservation FSDestinationWrapperAdapter::inspect_runtime_contract(
		const FSCompletenessProgram &p_program, const Dictionary &p_runtime_context) const {
	Error structural_error = OK;
	return inspect_runtime_contract_internal(p_program, p_runtime_context, structural_error);
}

Error FSDestinationWrapperAdapter::execute(const String &p_scratch_root,
		const Vector<FSCompletenessProgram> &p_programs, FSCompletenessRuntimeBatch &r_batch) const {
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
	// The family every case ID in this batch must be canonical under. A rendered program carries no
	// family, so the first program decides which of the adapter's families the batch belongs to and
	// every later one has to agree: a batch mixing two families would otherwise pass an identity check
	// that only ever asked "is this canonical under some family".
	String batch_family;
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

		const String pair_key = semantic_pair_key(program.coordinates, "surface");
		if (case_ids.has(program.case_id)) {
			return case_id_pair_keys[program.case_id] == pair_key &&
							case_id_surfaces[program.case_id] == surface
					? ERR_ALREADY_IN_USE
					: ERR_ALREADY_EXISTS;
		}
		case_ids.insert(program.case_id);
		case_id_pair_keys[program.case_id] = pair_key;
		case_id_surfaces[program.case_id] = surface;
		if (batch_family.is_empty()) {
			for (const String &candidate : FSDestinationWrapperAdapter::families()) {
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
	// The absolute size of the matrix is the manifest domain's business and is checked by the runner
	// against the resolved cells; here only that every program was filed under a surface is enforced.
	if (text_programs.size() + bytecode_programs.size() != p_programs.size()) {
		return ERR_INVALID_DATA;
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

	// How many surfaces a semantic pair must carry is the run's business, not the adapter's: a run
	// narrowed to one surface hands over one program per pair, and refusing it here would make the
	// filter unusable for this family. The adapter only refuses two programs for the same pair on the
	// same surface, which no cardinality the runner checks could tell apart from a correct matrix.
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

} // namespace FSTests
