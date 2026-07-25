#pragma once

#ifdef TOOLS_ENABLED

#include "modules/foundry_script/fs_conformance_registry.h"
#include "modules/foundry_script/fs_name_mangler_analysis.h"
#include "modules/foundry_script/fs_name_mangler_application.h"
#include "modules/foundry_script/tests/test_bytecode_serialization.h"

#include "tests/test_macros.h"

namespace FSTests {

class NameManglerRegistryRestore {
	String source;
	Vector<FSConformanceRegistry::RuntimeConformance> entries;

public:
	NameManglerRegistryRestore(
			const String &p_source,
			const Vector<FSConformanceRegistry::RuntimeConformance> &p_entries) :
			source(p_source),
			entries(p_entries) {}

	~NameManglerRegistryRestore() {
		FSConformanceRegistry::get_singleton()->register_runtime_witnesses(
				source, entries);
	}
};

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

TEST_CASE("[FoundryScript][NameManglerApplication] Renames only safe argument names") {
	const Ref<FoundryScript> script = compile_bytecode_test_source(
			"annotation private_marker_parameter targets PARAMETER\n"
			"\n"
			"signal private_marker_signal(@private_marker_parameter private_marker_signal_arg: int)\n"
			"\n"
			"enum PrivateMarkerMode:\n"
			"\tREADY = 1\n"
			"\tfunc private_marker_enum_method(private_marker_enum_arg: int) -> int:\n"
			"\t\treturn self + private_marker_enum_arg\n"
			"\n"
			"func private_marker_method("
			"@private_marker_parameter private_marker_arg: int, _fsb_arg_0: int) -> int:\n"
			"\treturn private_marker_arg + _fsb_arg_0\n"
			"\n"
			"@keep_name\n"
			"func kept_method(kept_parameter_marker: int) -> Callable:\n"
			"\treturn func(private_marker_lambda_arg: int) -> int:\n"
			"\t\treturn private_marker_lambda_arg + kept_parameter_marker\n"
			"\n"
			"@rpc func rpc_method(rpc_parameter_marker: int) -> void:\n"
			"\tpass\n");
	FSNameManglerAnalysis::Input input;
	input.scripts.push_back(script);
	const FSNameManglerAnalysis::Result analysis = FSNameManglerAnalysis::analyze(input);
	REQUIRE_EQ(analysis.error, OK);
	REQUIRE(analysis.rename_map.has(SNAME("private_marker_method")));
	REQUIRE(analysis.rename_map.has(SNAME("private_marker_signal")));
	REQUIRE(analysis.rename_map.has(SNAME("private_marker_enum_method")));
	REQUIRE_FALSE(analysis.rename_map.has(SNAME("kept_method")));
	REQUIRE_FALSE(analysis.rename_map.has(SNAME("rpc_method")));

	FSNameManglerApplication::Transaction transaction;
	Vector<FSNameManglerApplication::Diagnostic> diagnostics;
	REQUIRE_EQ(transaction.begin(input.scripts, analysis.rename_map, diagnostics), OK);

	const StringName renamed_method = analysis.rename_map[SNAME("private_marker_method")];
	const StringName renamed_signal = analysis.rename_map[SNAME("private_marker_signal")];
	const StringName renamed_enum = analysis.rename_map[SNAME("PrivateMarkerMode")];
	const StringName renamed_enum_method =
			analysis.rename_map[SNAME("private_marker_enum_method")];

	const HashMap<StringName, FSFunction *>::ConstIterator method_entry =
			script->get_member_functions().find(renamed_method);
	REQUIRE(method_entry);
	if (method_entry) {
		const MethodInfo private_info = method_entry->value->get_method_info();
		REQUIRE_EQ(private_info.arguments.size(), 2);
		CHECK(String(private_info.arguments[0].name).begins_with("_fsb_arg_"));
		CHECK(String(private_info.arguments[1].name).begins_with("_fsb_arg_"));
		CHECK_NE(private_info.arguments[0].name, private_info.arguments[1].name);
		CHECK_NE(private_info.arguments[0].name, SNAME("_fsb_arg_0"));
		CHECK_NE(private_info.arguments[1].name, SNAME("_fsb_arg_0"));
	}

	const HashMap<StringName, MethodInfo>::ConstIterator signal_entry =
			script->get_signals().find(renamed_signal);
	REQUIRE(signal_entry);
	if (signal_entry) {
		REQUIRE_EQ(signal_entry->value.arguments.size(), 1);
		CHECK(String(signal_entry->value.arguments[0].name).begins_with("_fsb_arg_"));
	}

	const FSFunction *enum_method =
			script->get_enum_function(renamed_enum, renamed_enum_method, false);
	REQUIRE(enum_method != nullptr);
	if (enum_method != nullptr) {
		const MethodInfo enum_info = enum_method->get_method_info();
		REQUIRE_EQ(enum_info.arguments.size(), 1);
		CHECK(String(enum_info.arguments[0].name).begins_with("_fsb_arg_"));
	}

	const HashMap<StringName, FSFunction *>::ConstIterator kept_entry =
			script->get_member_functions().find(SNAME("kept_method"));
	REQUIRE(kept_entry);
	if (kept_entry) {
		const MethodInfo kept_info = kept_entry->value->get_method_info();
		REQUIRE_EQ(kept_info.arguments.size(), 1);
		CHECK_EQ(kept_info.arguments[0].name, SNAME("kept_parameter_marker"));
		REQUIRE_EQ(kept_entry->value->get_lambdas().size(), 1);
		const MethodInfo lambda_info =
				kept_entry->value->get_lambdas()[0]->get_method_info();
		REQUIRE_FALSE(lambda_info.arguments.is_empty());
		for (const PropertyInfo &argument : lambda_info.arguments) {
			CHECK(String(argument.name).begins_with("_fsb_arg_"));
		}
	}

	const HashMap<StringName, FSFunction *>::ConstIterator rpc_entry =
			script->get_member_functions().find(SNAME("rpc_method"));
	REQUIRE(rpc_entry);
	if (rpc_entry) {
		const MethodInfo rpc_info = rpc_entry->value->get_method_info();
		REQUIRE_EQ(rpc_info.arguments.size(), 1);
		CHECK_EQ(rpc_info.arguments[0].name, SNAME("rpc_parameter_marker"));
	}

	const auto &method_parameter_annotations =
			script->get_method_parameter_annotations();
	REQUIRE(method_parameter_annotations.has(renamed_method));
	if (method_parameter_annotations.has(renamed_method) && method_entry) {
		const MethodInfo private_info = method_entry->value->get_method_info();
		CHECK(method_parameter_annotations[renamed_method].has(
				private_info.arguments[0].name));
		CHECK_FALSE(method_parameter_annotations[renamed_method].has(
				SNAME("private_marker_arg")));
	}

	const auto &signal_parameter_annotations =
			script->get_signal_parameter_annotations();
	REQUIRE(signal_parameter_annotations.has(renamed_signal));
	if (signal_parameter_annotations.has(renamed_signal) && signal_entry) {
		CHECK(signal_parameter_annotations[renamed_signal].has(
				signal_entry->value.arguments[0].name));
		CHECK_FALSE(signal_parameter_annotations[renamed_signal].has(
				SNAME("private_marker_signal_arg")));
	}
}

TEST_CASE("[FoundryScript][NameManglerApplication] Rejects protected mapped names atomically") {
	const Ref<FoundryScript> script = compile_bytecode_test_source(
			"extends Node\n"
			"\n"
			"var private_marker_member: int\n"
			"\n"
			"@rpc func rpc_method(rpc_parameter_marker: int) -> void:\n"
			"\tpass\n"
			"\n"
			"func private_marker_method(text: String) -> int:\n"
			"\tqueue_free()\n"
			"\tprint(text)\n"
			"\treturn text.length()\n");
	FSNameManglerAnalysis::Input input;
	input.scripts.push_back(script);
	const FSNameManglerAnalysis::Result analysis = FSNameManglerAnalysis::analyze(input);
	REQUIRE_EQ(analysis.error, OK);
	const Vector<uint8_t> baseline = name_mangler_application_serialize(script);

	const StringName protected_names[] = {
		SNAME("rpc_method"),
		SNAME("queue_free"),
		SNAME("length"),
		SNAME("print"),
	};
	for (int i = 0; i < 4; i++) {
		CAPTURE(protected_names[i]);
		RBMap<StringName, StringName> invalid_map = analysis.rename_map;
		invalid_map.insert(protected_names[i],
				StringName(vformat("_fsb_manual_%d", i)));

		FSNameManglerApplication::Transaction transaction;
		Vector<FSNameManglerApplication::Diagnostic> diagnostics;
		CHECK_NE(transaction.begin(input.scripts, invalid_map, diagnostics), OK);
		CHECK_FALSE(transaction.is_active());
		CHECK_EQ(transaction.get_state(),
				FSNameManglerApplication::Transaction::STATE_FINISHED);
		CHECK_FALSE(diagnostics.is_empty());
		if (!diagnostics.is_empty()) {
			CHECK(diagnostics[0].format().contains("protected"));
		}
		CHECK_EQ(name_mangler_application_serialize(script), baseline);
	}
}

TEST_CASE("[FoundryScript][NameManglerApplication] Requires a closed graph before mutation") {
	const Ref<FoundryScript> base = compile_bytecode_test_source(
			"class_name PrivateMarkerApplicationBase\n"
			"\n"
			"var private_marker_base_value: int = 7\n"
			"func private_marker_base_method() -> int:\n"
			"\treturn private_marker_base_value\n");
	const Ref<FoundryScript> derived = compile_bytecode_test_source(vformat(
			"extends \"%s\"\n"
			"\n"
			"func private_marker_derived_method() -> int:\n"
			"\treturn private_marker_base_method()\n",
			base->get_script_path()));

	FSNameManglerAnalysis::Input complete_input;
	complete_input.scripts.push_back(base);
	complete_input.scripts.push_back(derived);
	const FSNameManglerAnalysis::Result analysis =
			FSNameManglerAnalysis::analyze(complete_input);
	REQUIRE_EQ(analysis.error, OK);
	REQUIRE(analysis.rename_map.has(SNAME("private_marker_base_method")));

	const Vector<uint8_t> base_baseline =
			name_mangler_application_serialize(base);
	const Vector<uint8_t> derived_baseline =
			name_mangler_application_serialize(derived);
	Vector<Ref<FoundryScript>> incomplete_roots;
	incomplete_roots.push_back(derived);
	FSNameManglerApplication::Transaction transaction;
	Vector<FSNameManglerApplication::Diagnostic> diagnostics;
	CHECK_EQ(transaction.begin(incomplete_roots, analysis.rename_map, diagnostics),
			ERR_INVALID_PARAMETER);
	CHECK_FALSE(transaction.is_active());
	CHECK_EQ(transaction.get_state(),
			FSNameManglerApplication::Transaction::STATE_FINISHED);
	bool identifies_omitted_base = false;
	for (const FSNameManglerApplication::Diagnostic &diagnostic : diagnostics) {
		const String formatted = diagnostic.format();
		if (formatted.contains("closed") &&
				(formatted.contains(base->get_script_path()) ||
						formatted.contains(base->get_fully_qualified_name()))) {
			identifies_omitted_base = true;
			break;
		}
	}
	CHECK(identifies_omitted_base);
	CHECK_EQ(name_mangler_application_serialize(base), base_baseline);
	CHECK_EQ(name_mangler_application_serialize(derived), derived_baseline);
}

TEST_CASE("[FoundryScript][NameManglerApplication] Stages and restores the conformance registry") {
	const Ref<FoundryScript> script = compile_bytecode_test_source(
			"trait PrivateMarkerApplicationTrait:\n"
			"\tabstract func private_marker_witness(value: int) -> int\n"
			"\n"
			"class PrivateMarkerApplicationTarget:\n"
			"\tvar private_marker_target_value: int = 40\n"
			"\n"
			"extend PrivateMarkerApplicationTarget uses PrivateMarkerApplicationTrait:\n"
			"\tfunc private_marker_witness(value: int) -> int:\n"
			"\t\treturn private_marker_target_value + value\n");
	const String source = script->get_script_path();
	const Ref<FoundryScript> trait =
			script->get_subclasses()[SNAME("PrivateMarkerApplicationTrait")];
	const Ref<FoundryScript> target =
			script->get_subclasses()[SNAME("PrivateMarkerApplicationTarget")];
	const StringName original_trait = trait->get_trait_type_name();
	const String original_target = target->get_fully_qualified_name();
	const StringName original_method = SNAME("private_marker_witness");

	const Vector<FSConformanceRegistry::RuntimeConformance> compiler_entries =
			FSConformanceRegistry::get_singleton()->get_runtime_witnesses(source);
	NameManglerRegistryRestore registry_restore(source, compiler_entries);
	Vector<FSConformanceRegistry::RuntimeConformance> saved = compiler_entries;
	REQUIRE_EQ(saved.size(), 1);
	REQUIRE(saved[0].functions.has(original_method));
	saved.write[0].trait_name = original_trait;
	FSConformanceRegistry::get_singleton()->register_runtime_witnesses(source, saved);
	FSFunction *const original_function = saved[0].functions[original_method];
	REQUIRE(original_function != nullptr);
	REQUIRE_EQ(FSConformanceRegistry::get_singleton()->find_witness_function(
					   original_target, original_method),
			original_function);

	FSNameManglerAnalysis::Input input;
	input.scripts.push_back(script);
	const FSNameManglerAnalysis::Result analysis = FSNameManglerAnalysis::analyze(input);
	REQUIRE_EQ(analysis.error, OK);
	REQUIRE(analysis.rename_map.has(SNAME("PrivateMarkerApplicationTrait")));
	REQUIRE(analysis.rename_map.has(SNAME("PrivateMarkerApplicationTarget")));
	REQUIRE(analysis.rename_map.has(original_method));
	const Vector<uint8_t> baseline = name_mangler_application_serialize(script);

	FSNameManglerApplication::Transaction transaction;
	Vector<FSNameManglerApplication::Diagnostic> diagnostics;
	REQUIRE_EQ(transaction.begin(input.scripts, analysis.rename_map, diagnostics), OK);
	REQUIRE(diagnostics.is_empty());

	const StringName transformed_trait = trait->get_trait_type_name();
	const String transformed_target = target->get_fully_qualified_name();
	const StringName transformed_method = analysis.rename_map[original_method];
	const Vector<FSConformanceRegistry::RuntimeConformance> staged =
			FSConformanceRegistry::get_singleton()->get_runtime_witnesses(source);
	REQUIRE_EQ(staged.size(), 1);
	CHECK(staged[0].target_keys.has(transformed_target));
	CHECK_FALSE(staged[0].target_keys.has(original_target));
	CHECK_EQ(staged[0].trait_name, transformed_trait);
	const HashMap<StringName, FSFunction *>::ConstIterator staged_function =
			staged[0].functions.find(transformed_method);
	REQUIRE(staged_function);
	CHECK_FALSE(staged[0].functions.has(original_method));
	if (staged_function) {
		CHECK_EQ(staged_function->value, original_function);
	}
	CHECK_EQ(original_function->get_name(), transformed_method);
	CHECK_EQ(FSConformanceRegistry::get_singleton()->find_witness_function(
					 transformed_target, transformed_method),
			original_function);
	CHECK(FSConformanceRegistry::get_singleton()->find_witness_function(
				  original_target, original_method) == nullptr);
	CHECK(staged[0].target_keys.has(source));

	transaction.rollback();
	const Vector<FSConformanceRegistry::RuntimeConformance> restored =
			FSConformanceRegistry::get_singleton()->get_runtime_witnesses(source);
	REQUIRE_EQ(restored.size(), saved.size());
	for (int entry_index = 0; entry_index < saved.size(); entry_index++) {
		const FSConformanceRegistry::RuntimeConformance &expected =
				saved[entry_index];
		const FSConformanceRegistry::RuntimeConformance &actual =
				restored[entry_index];
		REQUIRE_EQ(actual.target_keys.size(), expected.target_keys.size());
		for (int key_index = 0; key_index < expected.target_keys.size();
				key_index++) {
			CHECK_EQ(actual.target_keys[key_index],
					expected.target_keys[key_index]);
		}
		CHECK_EQ(actual.trait_name, expected.trait_name);
		REQUIRE_EQ(actual.functions.size(), expected.functions.size());
		for (const KeyValue<StringName, FSFunction *> &function :
				expected.functions) {
			const HashMap<StringName, FSFunction *>::ConstIterator actual_function =
					actual.functions.find(function.key);
			REQUIRE(actual_function);
			if (actual_function) {
				CHECK_EQ(actual_function->value, function.value);
			}
		}
	}
	CHECK_EQ(original_function->get_name(), original_method);
	CHECK_EQ(FSConformanceRegistry::get_singleton()->find_witness_function(
					 original_target, original_method),
			original_function);
	CHECK_EQ(name_mangler_application_serialize(script), baseline);
}

} // namespace FSTests

#endif // TOOLS_ENABLED
