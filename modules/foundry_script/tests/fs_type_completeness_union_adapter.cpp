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

#include "../fs_analyzer.h"
#include "../fs_parser.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/os/os.h"

namespace FSTests {

namespace {

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
		return "\tvar stored: Variant = accept(SOURCE)";
	}
	return "\tvar holder := Holder.new()\n"
		   "\tholder.set(&\"value\", SOURCE)\n"
		   "\tvar stored: Variant = holder.value";
}

static void append_parser_diagnostics(const FSParser &p_parser, PackedStringArray &r_diagnostics) {
	for (const FSParser::ParserError &error : p_parser.get_errors()) {
		r_diagnostics.push_back(vformat("%d:%d: %s", error.line, error.column, error.message));
	}
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

} // namespace

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
BOUNDARY_BODY
	print(carrier_of(stored))
)FS";
	source = source.replace("DESTINATION", destination_type).replace("BOUNDARY_BODY", boundary_body);

	FSCompletenessProgram program;
	program.case_id = p_cell.case_id;
	program.surface = surface;
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
	const String path = vformat("user://type_completeness/%d/%s.fs",
			OS::get_singleton()->get_process_id(), p_program.case_id);
	const String absolute_directory = ProjectSettings::get_singleton()->globalize_path(path.get_base_dir());
	const Error directory_error = DirAccess::make_dir_recursive_absolute(absolute_directory);
	if (directory_error != OK) {
		observation.dimensions["analysis"] = "reject";
		observation.diagnostics.push_back(vformat("Could not create analyzer scratch directory: %d.", directory_error));
		return observation;
	}
	Error write_error = OK;
	{
		Ref<FileAccess> source_file = FileAccess::open(path, FileAccess::WRITE, &write_error);
		if (source_file.is_valid()) {
			source_file->store_string(p_program.source);
		}
	}
	if (write_error != OK) {
		observation.dimensions["analysis"] = "reject";
		observation.diagnostics.push_back(vformat("Could not write analyzer source: %d.", write_error));
		return observation;
	}
	FSParser parser;
	const Error parse_error = parser.parse(p_program.source, path, false);
	Error analyzer_error = ERR_PARSE_ERROR;
	if (parse_error == OK) {
		FSAnalyzer analyzer(&parser);
		analyzer_error = analyzer.analyze();
	}
	append_parser_diagnostics(parser, observation.diagnostics);
	observation.dimensions["analysis"] = parse_error == OK && analyzer_error == OK ? "accept" : "reject";
	return observation;
}

FSCompletenessObservation FSUnionCompletenessAdapter::inspect_runtime_contract(
		const FSCompletenessProgram &p_program, const Dictionary &) {
	FSCompletenessObservation observation;
	observation.case_id = p_program.case_id;
	observation.surface = p_program.surface;
	observation.diagnostics.push_back("Runtime contract inspection is not implemented.");
	return observation;
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
