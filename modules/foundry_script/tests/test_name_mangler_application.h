#pragma once

#ifdef TOOLS_ENABLED

#include "modules/foundry_script/fs_name_mangler_analysis.h"
#include "modules/foundry_script/fs_name_mangler_application.h"
#include "modules/foundry_script/tests/test_bytecode_serialization.h"

#include "tests/test_macros.h"

namespace FSTests {

static Vector<uint8_t> name_mangler_application_serialize(const Ref<FoundryScript> &p_script) {
	FSBytecodeExporter exporter;
	Vector<uint8_t> buffer;
	REQUIRE_EQ(exporter.serialize(p_script, buffer), OK);
	return buffer;
}

TEST_CASE("[FoundryScript][NameManglerApplication] Scoped transaction exposes lifecycle") {
	const Ref<FoundryScript> script = compile_bytecode_test_source(
			"var private_marker_member: int\n"
			"func private_marker_method(value: int) -> int:\n"
			"\tprivate_marker_member += value\n"
			"\treturn private_marker_member\n");
	FSNameManglerAnalysis::Input analysis_input;
	analysis_input.scripts.push_back(script);
	const FSNameManglerAnalysis::Result analysis = FSNameManglerAnalysis::analyze(analysis_input);

	FSNameManglerApplication::Transaction transaction;
	Vector<FSNameManglerApplication::Diagnostic> diagnostics;
	CHECK_EQ(transaction.get_state(), FSNameManglerApplication::Transaction::STATE_UNUSED);
	CHECK_EQ(transaction.begin(analysis_input.scripts, analysis.rename_map, diagnostics), OK);
	CHECK(transaction.is_active());
	transaction.rollback();
	CHECK_FALSE(transaction.is_active());
	CHECK_EQ(transaction.get_state(), FSNameManglerApplication::Transaction::STATE_FINISHED);
}

TEST_CASE("[FoundryScript][NameManglerApplication] Rewrites serialized surfaces and rolls back") {
	const Ref<FoundryScript> script = compile_bytecode_test_source(
			"namespace name_application\n"
			"class_name PrivateMarkerRoot\n"
			"\n"
			"var private_marker_member: int = 2\n"
			"static var private_marker_static: int = 3\n"
			"signal private_marker_signal(private_marker_signal_arg: int)\n"
			"const PrivateMarkerConstant = 4\n"
			"enum PrivateMarkerMode:\n"
			"\tREADY = 1\n"
			"\tfunc private_marker_enum_method(private_marker_enum_arg: int) -> int:\n"
			"\t\treturn self + private_marker_enum_arg\n"
			"\n"
			"class PrivateMarkerNested[T]:\n"
			"\tvar private_marker_nested_member: T\n"
			"\n"
			"func private_marker_method(private_marker_arg: int) -> int:\n"
			"\tvar private_marker_lambda := func(private_marker_lambda_arg: int) -> int:\n"
			"\t\treturn private_marker_lambda_arg + private_marker_member\n"
			"\tprivate_marker_signal.emit(private_marker_arg)\n"
			"\treturn private_marker_lambda.call(private_marker_arg)\n");
	FSNameManglerAnalysis::Input input;
	input.scripts.push_back(script);
	const FSNameManglerAnalysis::Result analysis = FSNameManglerAnalysis::analyze(input);
	REQUIRE_EQ(analysis.error, OK);
	REQUIRE(analysis.rename_map.has(SNAME("PrivateMarkerRoot")));
	REQUIRE(analysis.rename_map.has(SNAME("PrivateMarkerNested")));
	REQUIRE(analysis.rename_map.has(SNAME("private_marker_member")));
	REQUIRE(analysis.rename_map.has(SNAME("private_marker_static")));
	REQUIRE(analysis.rename_map.has(SNAME("private_marker_signal")));
	REQUIRE(analysis.rename_map.has(SNAME("PrivateMarkerConstant")));
	REQUIRE(analysis.rename_map.has(SNAME("PrivateMarkerMode")));
	REQUIRE(analysis.rename_map.has(SNAME("private_marker_enum_method")));
	REQUIRE(analysis.rename_map.has(SNAME("private_marker_method")));

	const Vector<uint8_t> baseline = name_mangler_application_serialize(script);
	const String original_fqcn = script->get_fully_qualified_name();

	FSNameManglerApplication::Transaction transaction;
	Vector<FSNameManglerApplication::Diagnostic> diagnostics;
	REQUIRE_EQ(transaction.begin(input.scripts, analysis.rename_map, diagnostics), OK);
	REQUIRE(diagnostics.is_empty());

	const StringName renamed_root = analysis.rename_map[SNAME("PrivateMarkerRoot")];
	const StringName renamed_nested = analysis.rename_map[SNAME("PrivateMarkerNested")];
	const StringName renamed_member = analysis.rename_map[SNAME("private_marker_member")];
	const StringName renamed_static = analysis.rename_map[SNAME("private_marker_static")];
	const StringName renamed_signal = analysis.rename_map[SNAME("private_marker_signal")];
	const StringName renamed_constant = analysis.rename_map[SNAME("PrivateMarkerConstant")];
	const StringName renamed_enum = analysis.rename_map[SNAME("PrivateMarkerMode")];
	const StringName renamed_enum_method = analysis.rename_map[SNAME("private_marker_enum_method")];
	const StringName renamed_method = analysis.rename_map[SNAME("private_marker_method")];

	CHECK_EQ(script->get_local_name(), renamed_root);
	CHECK(script->get_fully_qualified_name().ends_with(String(renamed_root)));
	CHECK(script->debug_get_member_indices().has(renamed_member));
	CHECK_FALSE(script->debug_get_member_indices().has(SNAME("private_marker_member")));
	CHECK_EQ(script->debug_get_static_var_by_index(0), renamed_static);
	CHECK(script->get_signals().has(renamed_signal));
	CHECK(script->get_constants().has(renamed_constant));
	CHECK(script->get_constants().has(renamed_enum));
	CHECK(script->get_member_functions().has(renamed_method));
	REQUIRE(script->get_subclasses().has(renamed_nested));
	const HashMap<StringName, Ref<FoundryScript>>::ConstIterator nested_entry =
			script->get_subclasses().find(renamed_nested);
	if (nested_entry) {
		CHECK(nested_entry->value->get_fully_qualified_name().ends_with("::" + String(renamed_nested)));
	}
	CHECK(script->get_enum_function(renamed_enum, renamed_enum_method, false) != nullptr);
	const HashMap<StringName, FSFunction *>::ConstIterator function_entry =
			script->get_member_functions().find(renamed_method);
	const FSFunction *function = function_entry ? function_entry->value : nullptr;
	REQUIRE(function != nullptr);
	if (function != nullptr) {
		CHECK_EQ(function->get_name(), renamed_method);
		bool renamed_dispatch_found = false;
		for (int i = 0; i < function->get_global_names_count(); i++) {
			if (function->get_global_name(i) == renamed_member ||
					function->get_global_name(i) == renamed_signal) {
				renamed_dispatch_found = true;
			}
		}
		CHECK(renamed_dispatch_found);
	}

	transaction.rollback();
	CHECK_EQ(script->get_fully_qualified_name(), original_fqcn);
	CHECK(script->debug_get_member_indices().has(SNAME("private_marker_member")));
	CHECK(script->get_signals().has(SNAME("private_marker_signal")));
	CHECK(script->get_constants().has(SNAME("PrivateMarkerConstant")));
	CHECK(script->get_member_functions().has(SNAME("private_marker_method")));
	CHECK_EQ(name_mangler_application_serialize(script), baseline);
}

} // namespace FSTests

#endif // TOOLS_ENABLED
