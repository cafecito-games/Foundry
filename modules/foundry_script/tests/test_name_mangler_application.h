#pragma once

#ifdef TOOLS_ENABLED

#include "modules/foundry_script/fs_conformance_registry.h"
#include "modules/foundry_script/fs_name_mangler_analysis.h"
#include "modules/foundry_script/fs_name_mangler_application.h"
#include "modules/foundry_script/tests/test_bytecode_serialization.h"

#include "tests/test_macros.h"

namespace FSTests {

class TestFSNameManglerApplicationAccessor {
public:
	struct CacheState {
		HashMap<StringName, FoundryScript::MemberInfo> old_static_variables_indices;
		HashMap<StringName, int> member_lines;
		HashMap<StringName, Variant> member_default_values;
		List<PropertyInfo> members_cache;
		HashMap<StringName, Variant> member_default_values_cache;
	};

	static CacheState capture_cache_state(const Ref<FoundryScript> &p_script) {
		CacheState state;
		state.old_static_variables_indices =
				p_script->old_static_variables_indices;
		state.member_lines = p_script->member_lines;
		state.member_default_values = p_script->member_default_values;
		state.members_cache = p_script->members_cache;
		state.member_default_values_cache =
				p_script->member_default_values_cache;
		return state;
	}

	static void seed_cache_state(
			const Ref<FoundryScript> &p_script,
			const StringName &p_member,
			const StringName &p_static_member) {
		REQUIRE(p_script->member_indices.has(p_member));
		REQUIRE(p_script->static_variables_indices.has(p_static_member));
		p_script->old_static_variables_indices.clear();
		p_script->old_static_variables_indices.insert(
				p_static_member,
				p_script->static_variables_indices[p_static_member]);
		p_script->member_lines.clear();
		p_script->member_lines.insert(p_member, 137);
		p_script->member_default_values.clear();
		p_script->member_default_values.insert(p_member, 41);
		p_script->members_cache.clear();
		p_script->members_cache.push_back(
				p_script->member_indices[p_member].property_info);
		p_script->member_default_values_cache.clear();
		p_script->member_default_values_cache.insert(p_member, 41);
	}

	static void seed_cached_property_dependency(
			const Ref<FoundryScript> &p_script,
			const StringName &p_dependency_name) {
		PropertyInfo dependency_property(
				Variant::OBJECT, "cached_dependency");
		dependency_property.class_name = p_dependency_name;
		p_script->members_cache.clear();
		p_script->members_cache.push_back(dependency_property);
	}

	static void seed_cached_property_info(
			const Ref<FoundryScript> &p_script,
			const PropertyInfo &p_property) {
		p_script->members_cache.clear();
		p_script->members_cache.push_back(p_property);
	}

	static void seed_old_static_dependency(
			const Ref<FoundryScript> &p_script,
			const Ref<FoundryScript> &p_dependency,
			const StringName &p_static_member) {
		REQUIRE(p_script->static_variables_indices.has(p_static_member));
		FoundryScript::MemberInfo info =
				p_script->static_variables_indices[p_static_member];
		info.data_type.kind = FSDataType::FOUNDRY_SCRIPT;
		info.data_type.script_type = p_dependency.ptr();
		info.data_type.script_type_ref = p_dependency;
		info.property_info.class_name = p_dependency->get_global_name();
		p_script->old_static_variables_indices.clear();
		p_script->old_static_variables_indices.insert(p_static_member, info);
	}

	static void seed_constant(
			const Ref<FoundryScript> &p_script,
			const StringName &p_name,
			const Variant &p_value) {
		p_script->constants.insert(p_name, p_value);
	}

	static bool has_staged_cache_keys(
			const Ref<FoundryScript> &p_script,
			const StringName &p_member,
			const StringName &p_static_member) {
		if (!p_script->old_static_variables_indices.has(p_static_member) ||
				!p_script->member_lines.has(p_member) ||
				!p_script->member_default_values.has(p_member) ||
				!p_script->member_default_values_cache.has(p_member) ||
				p_script->members_cache.size() != 1) {
			return false;
		}
		return StringName(p_script->members_cache.front()->get().name) ==
				p_member;
	}

	static bool cache_state_equals(
			const Ref<FoundryScript> &p_script,
			const CacheState &p_expected) {
		const auto type_argument_binding_equals =
				[](const FoundryScript::TypeArgumentBinding &p_left,
						const FoundryScript::TypeArgumentBinding &p_right) {
					return p_left.kind == p_right.kind &&
							p_left.fixed == p_right.fixed &&
							p_left.fixed_is_dependent ==
							p_right.fixed_is_dependent &&
							p_left.is_type_handle == p_right.is_type_handle &&
							p_left.leaf_ordinal == p_right.leaf_ordinal;
				};
		const auto member_info_equals =
				[&](const FoundryScript::MemberInfo &p_left,
						const FoundryScript::MemberInfo &p_right) {
					return p_left.index == p_right.index &&
							p_left.setter == p_right.setter &&
							p_left.getter == p_right.getter &&
							p_left.data_type == p_right.data_type &&
							p_left.property_info == p_right.property_info &&
							type_argument_binding_equals(
									p_left.type_argument_binding,
									p_right.type_argument_binding);
				};
		if (p_script->old_static_variables_indices.size() !=
				p_expected.old_static_variables_indices.size()) {
			return false;
		}
		for (const KeyValue<StringName, FoundryScript::MemberInfo> &entry :
				p_expected.old_static_variables_indices) {
			const FoundryScript::MemberInfo *actual =
					p_script->old_static_variables_indices.getptr(entry.key);
			if (actual == nullptr || !member_info_equals(*actual, entry.value)) {
				return false;
			}
		}
		if (p_script->member_lines.size() != p_expected.member_lines.size() ||
				p_script->member_default_values.size() !=
						p_expected.member_default_values.size() ||
				p_script->member_default_values_cache.size() !=
						p_expected.member_default_values_cache.size() ||
				p_script->members_cache.size() != p_expected.members_cache.size()) {
			return false;
		}
		for (const KeyValue<StringName, int> &entry :
				p_expected.member_lines) {
			const int *actual = p_script->member_lines.getptr(entry.key);
			if (actual == nullptr || *actual != entry.value) {
				return false;
			}
		}
		for (const KeyValue<StringName, Variant> &entry :
				p_expected.member_default_values) {
			const Variant *actual =
					p_script->member_default_values.getptr(entry.key);
			if (actual == nullptr || !actual->hash_compare(entry.value)) {
				return false;
			}
		}
		for (const KeyValue<StringName, Variant> &entry :
				p_expected.member_default_values_cache) {
			const Variant *actual =
					p_script->member_default_values_cache.getptr(entry.key);
			if (actual == nullptr || !actual->hash_compare(entry.value)) {
				return false;
			}
		}
		const List<PropertyInfo>::Element *actual_property =
				p_script->members_cache.front();
		const List<PropertyInfo>::Element *expected_property =
				p_expected.members_cache.front();
		while (actual_property != nullptr && expected_property != nullptr) {
			if (!(actual_property->get() == expected_property->get())) {
				return false;
			}
			actual_property = actual_property->next();
			expected_property = expected_property->next();
		}
		return actual_property == nullptr && expected_property == nullptr;
	}
};

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

class NameManglerGlobalClassRestore {
	StringName class_name;

public:
	explicit NameManglerGlobalClassRestore(const StringName &p_class_name) :
			class_name(p_class_name) {}

	~NameManglerGlobalClassRestore() {
		ScriptServer::remove_global_class(class_name);
	}
};

class NameManglerMultiFileRestore {
	Vector<String> paths;
	Vector<StringName> global_classes;
	Vector<String> conformance_sources;
	Vector<Ref<FoundryScript>> scripts;

public:
	void track_path(const String &p_path) {
		paths.push_back(p_path);
	}

	void register_global_class(
			const StringName &p_class_name,
			const StringName &p_base,
			const String &p_path,
			bool p_is_trait = false) {
		ScriptServer::remove_global_class(p_class_name);
		ScriptServer::add_global_class(
				p_class_name, p_base,
				FSLanguage::get_singleton()->get_name(), p_path,
				false, false, p_is_trait);
		global_classes.push_back(p_class_name);
	}

	void track_conformance_source(const String &p_source) {
		conformance_sources.push_back(p_source);
	}

	void track_script(const Ref<FoundryScript> &p_script) {
		scripts.push_back(p_script);
	}

	~NameManglerMultiFileRestore() {
		for (const String &source : conformance_sources) {
			FSConformanceRegistry::get_singleton()->clear_runtime_witnesses(
					source);
			FSConformanceRegistry::get_singleton()->clear_file(source);
		}
		for (const String &path : paths) {
			FSCache::remove_script(path);
			FSCache::remove_parser(path);
		}
		for (const StringName &global_class : global_classes) {
			ScriptServer::remove_global_class(global_class);
		}
		for (const String &path : paths) {
			DirAccess::remove_absolute(path);
		}
		scripts.clear();
	}
};

class NameManglerFixupGlobalRestore {
	StringName autoload_name;
	StringName named_global_name;
	String scene_path;

public:
	NameManglerFixupGlobalRestore(
			const StringName &p_autoload_name,
			const StringName &p_named_global_name,
			const String &p_scene_path) :
			autoload_name(p_autoload_name),
			named_global_name(p_named_global_name),
			scene_path(p_scene_path) {}

	~NameManglerFixupGlobalRestore() {
		FSLanguage::get_singleton()->set_compiling_for_export(false);
		if (FSLanguage::get_singleton()->get_named_globals_map().has(
					named_global_name)) {
			FSLanguage::get_singleton()->remove_named_global_constant(
					named_global_name);
		}
		if (ProjectSettings::get_singleton()->has_autoload(autoload_name)) {
			ProjectSettings::get_singleton()->remove_autoload(autoload_name);
		}
		if (ProjectSettings::get_singleton()->has_autoload(named_global_name)) {
			ProjectSettings::get_singleton()->remove_autoload(
					named_global_name);
		}
		if (FSLanguage::get_singleton()->get_global_map().has(autoload_name)) {
			TestFSLanguageGlobalsAccessor::remove_global(autoload_name);
		}
		DirAccess::remove_absolute(scene_path);
	}
};

static Vector<uint8_t> name_mangler_application_serialize(const Ref<FoundryScript> &p_script) {
	FSBytecodeExporter exporter;
	Vector<uint8_t> buffer;
	REQUIRE_EQ(exporter.serialize(p_script, buffer), OK);
	return buffer;
}

static int64_t name_mangler_application_run(
		const Ref<FoundryScript> &p_script, const StringName &p_method) {
	Callable::CallError call_error;
	const Variant instance_variant = p_script->_new(nullptr, 0, call_error);
	REQUIRE_EQ(call_error.error, Callable::CallError::CALL_OK);
	Object *instance = instance_variant;
	REQUIRE(instance != nullptr);
	const Variant result = instance->callp(p_method, nullptr, 0, call_error);
	CHECK_EQ(call_error.error, Callable::CallError::CALL_OK);
	return result;
}

static Ref<FoundryScript> name_mangler_application_load(
		const Vector<uint8_t> &p_buffer,
		const String &p_path,
		FSBytecodeExternalResolver *p_resolver) {
	Ref<FoundryScript> script;
	script.instantiate();
	script->set_path_cache(p_path);
	FSBytecodeLoader loader;
	loader.set_resolver(p_resolver);
	REQUIRE_EQ(loader.load_full(p_buffer, script), OK);
	return script;
}

static void name_mangler_application_write_source(
		const String &p_path, const String &p_source) {
	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::WRITE);
	REQUIRE(file.is_valid());
	file->store_string(p_source);
}

static Ref<FoundryScript> name_mangler_application_load_source(
		const String &p_path) {
	if (!FSLanguage::get_singleton()->get_reflection_singleton().is_valid()) {
		FSLanguage::get_singleton()->init();
	}
	const bool previous_ignore_warnings = FSParser::is_ignoring_warnings();
	FSParser::set_ignoring_warnings(true);
	Error error = OK;
	const Ref<FoundryScript> script =
			FSCache::get_full_script(p_path, error, String(), true);
	FSParser::set_ignoring_warnings(previous_ignore_warnings);
	REQUIRE_EQ(error, OK);
	REQUIRE(script.is_valid());
	REQUIRE(script->is_valid());
	return script;
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

TEST_CASE("[FoundryScript][NameManglerApplication] Restores nonserialized editor caches exactly") {
	const StringName member = SNAME("private_marker_cache_member");
	const StringName static_member = SNAME("private_marker_cache_static");
	const Ref<FoundryScript> script = compile_bytecode_test_source(
			"var private_marker_cache_member: int = 41\n"
			"static var private_marker_cache_static: int = 1\n");
	TestFSNameManglerApplicationAccessor::seed_cache_state(
			script, member, static_member);
	const TestFSNameManglerApplicationAccessor::CacheState baseline =
			TestFSNameManglerApplicationAccessor::capture_cache_state(script);

	FSNameManglerAnalysis::Input input;
	input.scripts.push_back(script);
	const FSNameManglerAnalysis::Result analysis =
			FSNameManglerAnalysis::analyze(input);
	REQUIRE_EQ(analysis.error, OK);
	REQUIRE(analysis.rename_map.has(member));
	REQUIRE(analysis.rename_map.has(static_member));
	const StringName renamed_member = analysis.rename_map[member];
	const StringName renamed_static = analysis.rename_map[static_member];

	{
		FSNameManglerApplication::Transaction transaction;
		Vector<FSNameManglerApplication::Diagnostic> diagnostics;
		REQUIRE_EQ(transaction.begin(
						   input.scripts, analysis.rename_map, diagnostics),
				OK);
		CHECK(TestFSNameManglerApplicationAccessor::has_staged_cache_keys(
				script, renamed_member, renamed_static));
		transaction.rollback();
	}
	CHECK(TestFSNameManglerApplicationAccessor::cache_state_equals(
			script, baseline));

	{
		FSNameManglerApplication::Transaction transaction;
		Vector<FSNameManglerApplication::Diagnostic> diagnostics;
		REQUIRE_EQ(transaction.begin(
						   input.scripts, analysis.rename_map, diagnostics),
				OK);
		CHECK(TestFSNameManglerApplicationAccessor::has_staged_cache_keys(
				script, renamed_member, renamed_static));
	}
	CHECK(TestFSNameManglerApplicationAccessor::cache_state_equals(
			script, baseline));
}

TEST_CASE("[FoundryScript][NameManglerApplication] Destructor restores after serialization error") {
	const StringName member = SNAME("private_marker_failure_member");
	const StringName static_member = SNAME("private_marker_failure_static");
	const StringName method = SNAME("private_marker_failure_method");
	const Ref<FoundryScript> script = compile_bytecode_test_source(
			"var private_marker_failure_member: int = 40\n"
			"static var private_marker_failure_static: int = 1\n"
			"func private_marker_failure_method(text: String) -> int:\n"
			"\treturn private_marker_failure_member + text.length()\n");
	TestFSNameManglerApplicationAccessor::seed_cache_state(
			script, member, static_member);
	const TestFSNameManglerApplicationAccessor::CacheState cache_baseline =
			TestFSNameManglerApplicationAccessor::capture_cache_state(script);
	const Vector<uint8_t> serialized_baseline =
			name_mangler_application_serialize(script);

	FSNameManglerAnalysis::Input input;
	input.scripts.push_back(script);
	const FSNameManglerAnalysis::Result analysis =
			FSNameManglerAnalysis::analyze(input);
	REQUIRE_EQ(analysis.error, OK);
	REQUIRE(analysis.rename_map.has(member));
	REQUIRE(analysis.rename_map.has(static_member));
	REQUIRE(analysis.rename_map.has(method));
	FSFunction *const function = script->get_member_functions()[method];
	REQUIRE(function != nullptr);
	const Vector<FSFunction::ExportFixups::TypedNameKey>
			original_builtin_fixups = function->export_fixups.builtin_methods;
	REQUIRE_FALSE(original_builtin_fixups.is_empty());
	function->export_fixups.builtin_methods.clear();

	{
		FSNameManglerApplication::Transaction transaction;
		Vector<FSNameManglerApplication::Diagnostic> diagnostics;
		REQUIRE_EQ(transaction.begin(
						   input.scripts, analysis.rename_map, diagnostics),
				OK);
		REQUIRE(transaction.is_active());
		CHECK_EQ(function->get_name(), analysis.rename_map[method]);
		CHECK(TestFSNameManglerApplicationAccessor::has_staged_cache_keys(
				script, analysis.rename_map[member],
				analysis.rename_map[static_member]));

		FSBytecodeExporter exporter;
		Vector<uint8_t> failed_buffer;
		ERR_PRINT_OFF;
		const Error serialize_error =
				exporter.serialize(script, failed_buffer);
		ERR_PRINT_ON;
		CHECK_EQ(serialize_error, ERR_INVALID_PARAMETER);
		CHECK(transaction.is_active());
	}

	CHECK(script->get_member_functions().has(method));
	CHECK_EQ(function->get_name(), method);
	CHECK(function->export_fixups.builtin_methods.is_empty());
	CHECK(TestFSNameManglerApplicationAccessor::cache_state_equals(
			script, cache_baseline));
	function->export_fixups.builtin_methods = original_builtin_fixups;
	CHECK_EQ(name_mangler_application_serialize(script),
			serialized_baseline);
}

TEST_CASE("[FoundryScript][NameManglerApplication] Preserves namespace segments that match mapped class names") {
	const Ref<FoundryScript> namespace_class = compile_bytecode_test_source(
			"class_name PrivateMarkerNamespace\n");
	const Ref<FoundryScript> target = compile_bytecode_test_source(
			"namespace PrivateMarkerNamespace.types\n"
			"class_name PrivateMarkerNamespacedTarget\n");
	const Ref<FoundryScript> consumer = compile_bytecode_test_source(vformat(
			"const Target = preload(\"%s\")\n"
			"func consume(value: Target) -> Target:\n"
			"\treturn value\n",
			target->get_script_path()));

	const HashMap<StringName, FSFunction *>::ConstIterator original_consume =
			consumer->get_member_functions().find(SNAME("consume"));
	REQUIRE(original_consume);
	REQUIRE_EQ(original_consume->value->get_method_info().arguments.size(), 1);
	const StringName original_type =
			original_consume->value->get_method_info().arguments[0].class_name;
	REQUIRE_EQ(original_type, target->get_global_name());

	Vector<Ref<FoundryScript>> roots;
	roots.push_back(namespace_class);
	roots.push_back(target);
	roots.push_back(consumer);
	RBMap<StringName, StringName> rename_map;
	rename_map.insert(SNAME("PrivateMarkerNamespace"),
			SNAME("_fsb_manual_namespace"));

	FSNameManglerApplication::Transaction transaction;
	Vector<FSNameManglerApplication::Diagnostic> diagnostics;
	REQUIRE_EQ(transaction.begin(roots, rename_map, diagnostics), OK);
	const HashMap<StringName, FSFunction *>::ConstIterator staged_consume =
			consumer->get_member_functions().find(SNAME("consume"));
	REQUIRE(staged_consume);
	REQUIRE_EQ(staged_consume->value->get_method_info().arguments.size(), 1);
	CHECK_EQ(staged_consume->value->get_method_info().arguments[0].class_name,
			original_type);
	CHECK_EQ(target->get_global_name(), original_type);
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
			"func private_marker_vararg("
			"@private_marker_parameter private_marker_fixed: int, "
			"...@private_marker_parameter private_marker_rest: Array) -> int:\n"
			"\treturn private_marker_fixed + private_marker_rest.size()\n"
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
	REQUIRE(analysis.rename_map.has(SNAME("private_marker_vararg")));
	REQUIRE(analysis.rename_map.has(SNAME("private_marker_signal")));
	REQUIRE(analysis.rename_map.has(SNAME("private_marker_enum_method")));
	REQUIRE_FALSE(analysis.rename_map.has(SNAME("kept_method")));
	REQUIRE_FALSE(analysis.rename_map.has(SNAME("rpc_method")));

	FSNameManglerApplication::Transaction transaction;
	Vector<FSNameManglerApplication::Diagnostic> diagnostics;
	REQUIRE_EQ(transaction.begin(input.scripts, analysis.rename_map, diagnostics), OK);

	const StringName renamed_method = analysis.rename_map[SNAME("private_marker_method")];
	const StringName renamed_vararg = analysis.rename_map[SNAME("private_marker_vararg")];
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
	REQUIRE(method_parameter_annotations.has(renamed_vararg));
	if (method_parameter_annotations.has(renamed_vararg)) {
		const HashMap<StringName, Vector<FoundryScript::AnnotationUsage>> &parameters =
				method_parameter_annotations[renamed_vararg];
		REQUIRE_EQ(parameters.size(), 2);
		CHECK_FALSE(parameters.has(SNAME("private_marker_fixed")));
		CHECK_FALSE(parameters.has(SNAME("private_marker_rest")));
		for (const KeyValue<StringName,
					 Vector<FoundryScript::AnnotationUsage>> &parameter :
				parameters) {
			CHECK(String(parameter.key).begins_with("_fsb_arg_"));
		}
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
			"var private_marker_annotation_name: int\n"
			"\n"
			"annotation marker(value: String) targets METHOD\n"
			"@marker(\"private_marker_annotation_name\")\n"
			"func annotated_method() -> void:\n"
			"\tpass\n"
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
		SNAME("private_marker_annotation_name"),
	};
	for (int i = 0; i < 5; i++) {
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

TEST_CASE("[FoundryScript][NameManglerApplication] Protects every pointer-fixup name table") {
	if (!FSLanguage::get_singleton()->has_any_global_constant(
				SNAME("RefCounted"))) {
		FSLanguage::get_singleton()->init();
	}
	const StringName autoload_name =
			SNAME("PrivateMarkerFixupAutoload");
	const StringName named_global_name =
			SNAME("PrivateMarkerFixupNamedGlobal");
	const String scene_path =
			TestUtils::get_temp_path("name_mangler_fixup_autoload.tscn");
	NameManglerFixupGlobalRestore global_restore(
			autoload_name, named_global_name, scene_path);
	{
		Ref<FileAccess> scene_file =
				FileAccess::open(scene_path, FileAccess::WRITE);
		REQUIRE(scene_file.is_valid());
		scene_file->store_string(
				"[gd_scene format=3]\n\n[node name=\"Root\" type=\"Node\"]\n");
	}
	ProjectSettings::AutoloadInfo autoload;
	autoload.name = autoload_name;
	autoload.path = scene_path;
	autoload.is_singleton = true;
	ProjectSettings::get_singleton()->add_autoload(autoload);
	ProjectSettings::AutoloadInfo named_global_autoload;
	named_global_autoload.name = named_global_name;
	named_global_autoload.path = scene_path;
	named_global_autoload.is_singleton = true;
	ProjectSettings::get_singleton()->add_autoload(
			named_global_autoload);
	FSLanguage::get_singleton()->add_global_constant(
			autoload_name, Variant());
	FSLanguage::get_singleton()->add_named_global_constant(
			named_global_name, Variant());

	const Ref<FoundryScript> script = compile_bytecode_test_source(
			"func private_marker_fixup_probe() -> Array:\n"
			"\tvar base := 1.5\n"
			"\tvar vector := Vector2(base, base + 1.0)\n"
			"\tvector.x = 3.0\n"
			"\tvar vertical := vector.y\n"
			"\tvar total := vector.x + vertical\n"
			"\tvar negated := -total\n"
			"\tvar packed := PackedFloat64Array()\n"
			"\tpacked.resize(2)\n"
			"\tpacked[0] = total\n"
			"\tvar first := packed[0]\n"
			"\tvar data := {}\n"
			"\tdata[\"value\"] = negated\n"
			"\tvar stored = data[\"value\"]\n"
			"\tvar text := \"fixup\"\n"
			"\tvar text_length := text.length()\n"
			"\tvar absolute := absf(total + first)\n"
			"\tvar count := len(text)\n"
			"\tvar dynamic_left: Variant = 1\n"
			"\tvar dynamic_right: Variant = 2\n"
			"\tvar dynamic_sum = dynamic_left + dynamic_right\n"
			"\tvar reference := RefCounted.new()\n"
			"\tvar identifier := reference.get_instance_id()\n"
			"\tvar autoload_value = PrivateMarkerFixupAutoload\n"
			"\tvar named_value = PrivateMarkerFixupNamedGlobal\n"
			"\treturn [stored, absolute, count, text_length, identifier, "
			"autoload_value, named_value, dynamic_sum]\n"
			"\n"
			"@keep_name\n"
			"func run() -> int:\n"
			"\treturn int(private_marker_fixup_probe()[7])\n");
	const HashMap<StringName, FSFunction *>::ConstIterator function_entry =
			script->get_member_functions().find(
					SNAME("private_marker_fixup_probe"));
	REQUIRE(function_entry);
	FSFunction *const function = function_entry->value;
	REQUIRE(function != nullptr);
	const FSFunction::ExportFixups baseline_fixups =
			function->export_fixups;

	const auto check_fixups =
			[&](const FSFunction::ExportFixups &p_fixups) {
				REQUIRE_EQ(p_fixups.operators.size(),
						baseline_fixups.operators.size());
				REQUIRE_EQ(p_fixups.setters.size(),
						baseline_fixups.setters.size());
				REQUIRE_EQ(p_fixups.getters.size(),
						baseline_fixups.getters.size());
				REQUIRE_EQ(p_fixups.keyed_setters,
						baseline_fixups.keyed_setters);
				REQUIRE_EQ(p_fixups.keyed_getters,
						baseline_fixups.keyed_getters);
				REQUIRE_EQ(p_fixups.indexed_setters,
						baseline_fixups.indexed_setters);
				REQUIRE_EQ(p_fixups.indexed_getters,
						baseline_fixups.indexed_getters);
				REQUIRE_EQ(p_fixups.builtin_methods.size(),
						baseline_fixups.builtin_methods.size());
				REQUIRE_EQ(p_fixups.constructors.size(),
						baseline_fixups.constructors.size());
				REQUIRE_EQ(p_fixups.utilities, baseline_fixups.utilities);
				REQUIRE_EQ(p_fixups.gds_utilities,
						baseline_fixups.gds_utilities);
				REQUIRE_EQ(p_fixups.method_binds.size(),
						baseline_fixups.method_binds.size());
				REQUIRE_EQ(p_fixups.global_stores.size(),
						baseline_fixups.global_stores.size());
				REQUIRE_EQ(p_fixups.operator_cache_offsets,
						baseline_fixups.operator_cache_offsets);
				REQUIRE_EQ(p_fixups.named_globals,
						baseline_fixups.named_globals);
				for (int i = 0; i < p_fixups.operators.size(); i++) {
					CHECK_EQ(p_fixups.operators[i].op,
							baseline_fixups.operators[i].op);
					CHECK_EQ(p_fixups.operators[i].left_type,
							baseline_fixups.operators[i].left_type);
					CHECK_EQ(p_fixups.operators[i].right_type,
							baseline_fixups.operators[i].right_type);
				}
				for (int i = 0; i < p_fixups.setters.size(); i++) {
					CHECK_EQ(p_fixups.setters[i].type,
							baseline_fixups.setters[i].type);
					CHECK_EQ(p_fixups.setters[i].name,
							baseline_fixups.setters[i].name);
				}
				for (int i = 0; i < p_fixups.getters.size(); i++) {
					CHECK_EQ(p_fixups.getters[i].type,
							baseline_fixups.getters[i].type);
					CHECK_EQ(p_fixups.getters[i].name,
							baseline_fixups.getters[i].name);
				}
				for (int i = 0; i < p_fixups.builtin_methods.size(); i++) {
					CHECK_EQ(p_fixups.builtin_methods[i].type,
							baseline_fixups.builtin_methods[i].type);
					CHECK_EQ(p_fixups.builtin_methods[i].name,
							baseline_fixups.builtin_methods[i].name);
				}
				for (int i = 0; i < p_fixups.constructors.size(); i++) {
					CHECK_EQ(p_fixups.constructors[i].type,
							baseline_fixups.constructors[i].type);
					CHECK_EQ(p_fixups.constructors[i].constructor_index,
							baseline_fixups.constructors[i].constructor_index);
				}
				for (int i = 0; i < p_fixups.method_binds.size(); i++) {
					CHECK_EQ(p_fixups.method_binds[i].class_name,
							baseline_fixups.method_binds[i].class_name);
					CHECK_EQ(p_fixups.method_binds[i].method_name,
							baseline_fixups.method_binds[i].method_name);
				}
				for (int i = 0; i < p_fixups.global_stores.size(); i++) {
					CHECK_EQ(p_fixups.global_stores[i].code_offset,
							baseline_fixups.global_stores[i].code_offset);
					CHECK_EQ(p_fixups.global_stores[i].global_name,
							baseline_fixups.global_stores[i].global_name);
				}
			};
	REQUIRE_FALSE(baseline_fixups.operators.is_empty());
	REQUIRE_FALSE(baseline_fixups.setters.is_empty());
	REQUIRE_FALSE(baseline_fixups.getters.is_empty());
	REQUIRE_FALSE(baseline_fixups.keyed_setters.is_empty());
	REQUIRE_FALSE(baseline_fixups.keyed_getters.is_empty());
	REQUIRE_FALSE(baseline_fixups.indexed_setters.is_empty());
	REQUIRE_FALSE(baseline_fixups.indexed_getters.is_empty());
	REQUIRE_FALSE(baseline_fixups.builtin_methods.is_empty());
	REQUIRE_FALSE(baseline_fixups.constructors.is_empty());
	REQUIRE(baseline_fixups.utilities.has(SNAME("absf")));
	REQUIRE(baseline_fixups.gds_utilities.has(SNAME("len")));
	REQUIRE_FALSE(baseline_fixups.method_binds.is_empty());
	REQUIRE_FALSE(baseline_fixups.global_stores.is_empty());
	REQUIRE_FALSE(
			baseline_fixups.operator_cache_offsets.is_empty());
	REQUIRE(baseline_fixups.named_globals.has(named_global_name));
	FSBytecodeExporter exporter;
	const Vector<StringName> unsupported_named_globals =
			exporter.collect_unsupported_named_globals(script);
	REQUIRE(unsupported_named_globals.has(named_global_name));
	CHECK_EQ(name_mangler_application_run(script, SNAME("run")), 3);

	FSNameManglerAnalysis::Input input;
	input.scripts.push_back(script);
	const FSNameManglerAnalysis::Result analysis =
			FSNameManglerAnalysis::analyze(input);
	REQUIRE_EQ(analysis.error, OK);
	REQUIRE(analysis.rename_map.has(
			SNAME("private_marker_fixup_probe")));
	Vector<uint8_t> staged_buffer;
	{
		FSNameManglerApplication::Transaction transaction;
		Vector<FSNameManglerApplication::Diagnostic> diagnostics;
		REQUIRE_EQ(transaction.begin(
						   input.scripts, analysis.rename_map, diagnostics),
				OK);
		check_fixups(function->export_fixups);
		CHECK_EQ(name_mangler_application_run(script, SNAME("run")), 3);
		staged_buffer = name_mangler_application_serialize(script);
	}
	check_fixups(function->export_fixups);
	CHECK_EQ(name_mangler_application_run(script, SNAME("run")), 3);
	BytecodeTestResolver resolver;
	const Ref<FoundryScript> loaded = name_mangler_application_load(
			staged_buffer, script->get_script_path(), &resolver);
	CHECK_EQ(name_mangler_application_run(loaded, SNAME("run")), 3);

	const StringName protected_names[] = {
		SNAME("x"),
		SNAME("y"),
		SNAME("length"),
		SNAME("absf"),
		SNAME("len"),
		SNAME("get_instance_id"),
		autoload_name,
		named_global_name,
	};
	for (int i = 0; i < 8; i++) {
		CAPTURE(protected_names[i]);
		RBMap<StringName, StringName> invalid = analysis.rename_map;
		invalid.insert(
				protected_names[i],
				StringName(vformat("_fsb_fixup_%d", i)));
		FSNameManglerApplication::Transaction transaction;
		Vector<FSNameManglerApplication::Diagnostic> diagnostics;
		CHECK_EQ(transaction.begin(input.scripts, invalid, diagnostics),
				ERR_INVALID_PARAMETER);
		CHECK_FALSE(transaction.is_active());
		CHECK_FALSE(diagnostics.is_empty());
		check_fixups(function->export_fixups);
	}
}

TEST_CASE("[FoundryScript][NameManglerApplication] Protects native type identities globally") {
	const Ref<FoundryScript> declaration = compile_bytecode_test_source(
			"func Node2D() -> int:\n"
			"\treturn 1\n");
	const Ref<FoundryScript> typed = compile_bytecode_test_source(
			"func accept_native(value: Node2D) -> Node2D:\n"
			"\treturn value\n");
	FSNameManglerAnalysis::Input input;
	input.scripts.push_back(declaration);
	input.scripts.push_back(typed);
	const FSNameManglerAnalysis::Result analysis = FSNameManglerAnalysis::analyze(input);
	REQUIRE_EQ(analysis.error, OK);
	CHECK_FALSE(analysis.rename_map.has(SNAME("Node2D")));

	const HashMap<StringName, FSFunction *>::ConstIterator accept =
			typed->get_member_functions().find(SNAME("accept_native"));
	REQUIRE(accept);
	FSFunction *const accept_function = accept->value;
	REQUIRE(accept_function != nullptr);
	REQUIRE_EQ(accept_function->get_method_info().arguments.size(), 1);
	CHECK_EQ(accept_function->get_method_info().arguments[0].class_name,
			SNAME("Node2D"));

	RBMap<StringName, StringName> invalid_map = analysis.rename_map;
	invalid_map.insert(SNAME("Node2D"), SNAME("_fsb_manual_native"));
	FSNameManglerApplication::Transaction transaction;
	Vector<FSNameManglerApplication::Diagnostic> diagnostics;
	CHECK_EQ(transaction.begin(input.scripts, invalid_map, diagnostics),
			ERR_INVALID_PARAMETER);
	CHECK_FALSE(transaction.is_active());
	CHECK_FALSE(diagnostics.is_empty());
	if (transaction.is_active()) {
		transaction.rollback();
	}
	CHECK_EQ(accept_function->get_method_info().arguments[0].class_name,
			SNAME("Node2D"));
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

TEST_CASE("[FoundryScript][NameManglerApplication] Rejects omitted annotation dependencies") {
	const Ref<FoundryScript> dependency = compile_bytecode_test_source(
			"class_name PrivateMarkerAnnotationDependency\n");
	const Ref<FoundryScript> host = compile_bytecode_test_source(vformat(
			"annotation dependency(value: Variant) targets METHOD\n"
			"\n"
			"@dependency(preload(\"%s\"))\n"
			"func annotated_method() -> void:\n"
			"\tpass\n",
			dependency->get_script_path()));

	const Vector<uint8_t> baseline = name_mangler_application_serialize(host);
	Vector<Ref<FoundryScript>> incomplete_roots;
	incomplete_roots.push_back(host);
	FSNameManglerApplication::Transaction transaction;
	Vector<FSNameManglerApplication::Diagnostic> diagnostics;
	CHECK_EQ(transaction.begin(incomplete_roots, {}, diagnostics),
			ERR_INVALID_PARAMETER);
	CHECK_FALSE(transaction.is_active());
	bool identifies_annotation_dependency = false;
	for (const FSNameManglerApplication::Diagnostic &diagnostic : diagnostics) {
		if (diagnostic.format().contains(dependency->get_script_path())) {
			identifies_annotation_dependency = true;
			break;
		}
	}
	CHECK(identifies_annotation_dependency);
	CHECK_EQ(name_mangler_application_serialize(host), baseline);
}

TEST_CASE("[FoundryScript][NameManglerApplication] Rejects omitted PropertyInfo dependencies") {
	const StringName dependency_name =
			SNAME("PrivateMarkerPropertyInfoDependency");
	const Ref<FoundryScript> dependency = compile_bytecode_test_source(
			"class_name PrivateMarkerPropertyInfoDependency\n"
			"\n"
			"enum PrivateMarkerPropertyInfoMode:\n"
			"\tREADY = 1\n");
	REQUIRE_FALSE(ScriptServer::is_global_class(dependency_name));
	ScriptServer::add_global_class(dependency_name, SNAME("RefCounted"),
			FSLanguage::get_singleton()->get_name(),
			dependency->get_script_path(), false, false, false);
	NameManglerGlobalClassRestore global_class_restore(dependency_name);

	SUBCASE("Direct class name") {
		const Ref<FoundryScript> host = compile_bytecode_test_source(
				"signal dependency_signal(value: "
				"PrivateMarkerPropertyInfoDependency)\n");
		const HashMap<StringName, MethodInfo>::ConstIterator signal =
				host->get_signals().find(SNAME("dependency_signal"));
		REQUIRE(signal);
		REQUIRE_EQ(signal->value.arguments.size(), 1);
		REQUIRE_EQ(signal->value.arguments[0].class_name,
				dependency_name);

		Vector<Ref<FoundryScript>> incomplete_roots;
		incomplete_roots.push_back(host);
		FSNameManglerApplication::Transaction transaction;
		Vector<FSNameManglerApplication::Diagnostic> diagnostics;
		CHECK_EQ(transaction.begin(incomplete_roots, {}, diagnostics),
				ERR_INVALID_PARAMETER);
		CHECK_FALSE(transaction.is_active());
		bool identifies_property_dependency = false;
		for (const FSNameManglerApplication::Diagnostic &diagnostic :
				diagnostics) {
			if (diagnostic.format().contains(String(dependency_name))) {
				identifies_property_dependency = true;
				break;
			}
		}
		CHECK(identifies_property_dependency);
	}

	SUBCASE("Type-bearing hint grammar") {
		const Ref<FoundryScript> host = compile_bytecode_test_source(
				"signal dependency_signal(values: "
				"Array[PrivateMarkerPropertyInfoDependency])\n");
		const HashMap<StringName, MethodInfo>::ConstIterator signal =
				host->get_signals().find(SNAME("dependency_signal"));
		REQUIRE(signal);
		REQUIRE_EQ(signal->value.arguments.size(), 1);
		CHECK(signal->value.arguments[0].hint_string.contains(
				String(dependency_name)));

		Vector<Ref<FoundryScript>> incomplete_roots;
		incomplete_roots.push_back(host);
		FSNameManglerApplication::Transaction transaction;
		Vector<FSNameManglerApplication::Diagnostic> diagnostics;
		CHECK_EQ(transaction.begin(incomplete_roots, {}, diagnostics),
				ERR_INVALID_PARAMETER);
		CHECK_FALSE(transaction.is_active());
		CHECK_FALSE(diagnostics.is_empty());
	}

	SUBCASE("Class enum owner") {
		const String enum_identity =
				String(dependency_name) +
				".PrivateMarkerPropertyInfoMode";
		const Ref<FoundryScript> host = compile_bytecode_test_source(
				"signal dependency_signal(value: "
				"PrivateMarkerPropertyInfoDependency."
				"PrivateMarkerPropertyInfoMode)\n");
		const HashMap<StringName, MethodInfo>::ConstIterator signal =
				host->get_signals().find(SNAME("dependency_signal"));
		REQUIRE(signal);
		REQUIRE_EQ(signal->value.arguments.size(), 1);
		CHECK_EQ(String(signal->value.arguments[0].class_name),
				enum_identity);
		CHECK_NE(signal->value.arguments[0].usage &
						PROPERTY_USAGE_CLASS_IS_ENUM,
				0);

		Vector<Ref<FoundryScript>> incomplete_roots;
		incomplete_roots.push_back(host);
		FSNameManglerApplication::Transaction transaction;
		Vector<FSNameManglerApplication::Diagnostic> diagnostics;
		CHECK_EQ(transaction.begin(incomplete_roots, {}, diagnostics),
				ERR_INVALID_PARAMETER);
		CHECK_FALSE(transaction.is_active());
		bool identifies_enum_owner = false;
		for (const FSNameManglerApplication::Diagnostic &diagnostic :
				diagnostics) {
			if (diagnostic.format().contains(
						String(dependency_name))) {
				identifies_enum_owner = true;
				break;
			}
		}
		CHECK(identifies_enum_owner);

		Vector<Ref<FoundryScript>> complete_roots;
		complete_roots.push_back(host);
		complete_roots.push_back(dependency);
		FSNameManglerApplication::Transaction complete_transaction;
		diagnostics.clear();
		CHECK_EQ(complete_transaction.begin(
						 complete_roots, {}, diagnostics),
				OK);
		CHECK(complete_transaction.is_active());
		CHECK(diagnostics.is_empty());
		complete_transaction.rollback();
	}
}

TEST_CASE("[FoundryScript][NameManglerApplication] Resolves the longest enum owner and exempts native enums") {
	const StringName short_name =
			SNAME("PrivateMarkerEnumScope");
	const StringName long_name =
			SNAME("PrivateMarkerEnumScope.Owner");
	const Ref<FoundryScript> short_dependency =
			compile_bytecode_test_source(
					"class_name PrivateMarkerEnumShortOwner\n");
	const Ref<FoundryScript> long_dependency =
			compile_bytecode_test_source(
					"class_name PrivateMarkerEnumLongOwner\n");
	REQUIRE_FALSE(ScriptServer::is_global_class(short_name));
	REQUIRE_FALSE(ScriptServer::is_global_class(long_name));
	ScriptServer::add_global_class(
			short_name, SNAME("RefCounted"),
			FSLanguage::get_singleton()->get_name(),
			short_dependency->get_script_path(), false, false, false);
	NameManglerGlobalClassRestore short_restore(short_name);
	ScriptServer::add_global_class(
			long_name, SNAME("RefCounted"),
			FSLanguage::get_singleton()->get_name(),
			long_dependency->get_script_path(), false, false, false);
	NameManglerGlobalClassRestore long_restore(long_name);

	SUBCASE("Longest Foundry global prefix") {
		const Ref<FoundryScript> host =
				compile_bytecode_test_source("var host_value: int\n");
		PropertyInfo enum_property(
				Variant::INT, "enum_value", PROPERTY_HINT_NONE, "",
				PROPERTY_USAGE_DEFAULT |
						PROPERTY_USAGE_CLASS_IS_ENUM,
				"PrivateMarkerEnumScope.Owner.Mode");
		TestFSNameManglerApplicationAccessor::seed_cached_property_info(
				host, enum_property);
		Vector<Ref<FoundryScript>> roots;
		roots.push_back(host);
		roots.push_back(long_dependency);
		FSNameManglerApplication::Transaction transaction;
		Vector<FSNameManglerApplication::Diagnostic> diagnostics;
		CHECK_EQ(transaction.begin(roots, {}, diagnostics), OK);
		CHECK(transaction.is_active());
		CHECK(diagnostics.is_empty());
		transaction.rollback();
	}

	SUBCASE("Native and builtin enum owners") {
		const StringName native_enum_identities[] = {
			SNAME("Node.ProcessMode"),
			SNAME("Variant.Type"),
			SNAME("Vector3.Axis"),
		};
		for (const StringName &identity : native_enum_identities) {
			CAPTURE(identity);
			const Ref<FoundryScript> host =
					compile_bytecode_test_source(
							"var host_value: int\n");
			PropertyInfo enum_property(
					Variant::INT, "enum_value", PROPERTY_HINT_NONE, "",
					PROPERTY_USAGE_DEFAULT |
							PROPERTY_USAGE_CLASS_IS_ENUM,
					identity);
			TestFSNameManglerApplicationAccessor::seed_cached_property_info(
					host, enum_property);
			Vector<Ref<FoundryScript>> roots;
			roots.push_back(host);
			FSNameManglerApplication::Transaction transaction;
			Vector<FSNameManglerApplication::Diagnostic> diagnostics;
			CHECK_EQ(transaction.begin(roots, {}, diagnostics), OK);
			CHECK(transaction.is_active());
			CHECK(diagnostics.is_empty());
			transaction.rollback();
		}
	}
}

TEST_CASE("[FoundryScript][NameManglerApplication] Rejects recursive FSDataType dependencies") {
	const StringName dependency_name =
			SNAME("PrivateMarkerFSDataTypeDependency");
	const Ref<FoundryScript> dependency = compile_bytecode_test_source(
			"class_name PrivateMarkerFSDataTypeDependency\n");
	REQUIRE_FALSE(ScriptServer::is_global_class(dependency_name));
	ScriptServer::add_global_class(
			dependency_name, SNAME("RefCounted"),
			FSLanguage::get_singleton()->get_name(),
			dependency->get_script_path(), false, false, false);
	NameManglerGlobalClassRestore global_class_restore(dependency_name);

	const auto expect_function_argument_rejected =
			[](const Ref<FoundryScript> &p_host) {
				Vector<Ref<FoundryScript>> incomplete_roots;
				incomplete_roots.push_back(p_host);
				FSNameManglerApplication::Transaction transaction;
				Vector<FSNameManglerApplication::Diagnostic> diagnostics;
				CHECK_EQ(transaction.begin(
								 incomplete_roots, {}, diagnostics),
						ERR_INVALID_PARAMETER);
				CHECK_FALSE(transaction.is_active());
				bool identifies_function_argument = false;
				for (const FSNameManglerApplication::Diagnostic &diagnostic :
						diagnostics) {
					if (diagnostic.format().contains(
								"function argument type")) {
						identifies_function_argument = true;
						break;
					}
				}
				CHECK(identifies_function_argument);
			};

	SUBCASE("Container element type") {
		const Ref<FoundryScript> host = compile_bytecode_test_source(
				"func take(values: "
				"Array[PrivateMarkerFSDataTypeDependency]) -> void:\n"
				"\tpass\n");
		expect_function_argument_rejected(host);
	}

	SUBCASE("Generic type argument") {
		const Ref<FoundryScript> host = compile_bytecode_test_source(
				"class_name PrivateMarkerFSDataTypeHost[T]\n"
				"\n"
				"func take(value: "
				"PrivateMarkerFSDataTypeHost["
				"PrivateMarkerFSDataTypeDependency]) -> void:\n"
				"\tpass\n");
		expect_function_argument_rejected(host);
	}
}

TEST_CASE("[FoundryScript][NameManglerApplication] Validates tagged specialized and container dependencies") {
	const Ref<FoundryScript> dependency = compile_bytecode_test_source(
			"class_name PrivateMarkerTaggedDependency[T]\n");

	const auto expect_rejected =
			[&](const Ref<FoundryScript> &p_host) {
				Vector<Ref<FoundryScript>> incomplete_roots;
				incomplete_roots.push_back(p_host);
				FSNameManglerApplication::Transaction transaction;
				Vector<FSNameManglerApplication::Diagnostic> diagnostics;
				CHECK_EQ(transaction.begin(
								 incomplete_roots, {}, diagnostics),
						ERR_INVALID_PARAMETER);
				CHECK_FALSE(transaction.is_active());
				CHECK_FALSE(diagnostics.is_empty());
			};

	SUBCASE("Specialized handle target") {
		const Ref<FoundryScript> host =
				compile_bytecode_test_source("var host_value: int\n");
		ContainerType integer_argument;
		integer_argument.builtin_type = Variant::INT;
		Vector<ContainerType> type_arguments;
		type_arguments.push_back(integer_argument);
		const Ref<FSSpecializedClassHandle> handle =
				FSSpecializedClassHandle::create(
						dependency, type_arguments);
		Array nested_array;
		nested_array.push_back(handle);
		Dictionary nested_dictionary;
		nested_dictionary["specialized"] = nested_array;
		TestFSNameManglerApplicationAccessor::seed_constant(
				host, SNAME("TaggedDependency"), nested_dictionary);
		expect_rejected(host);
	}

	SUBCASE("Recursive container type argument") {
		const Ref<FoundryScript> host =
				compile_bytecode_test_source("var host_value: int\n");
		ContainerType dependent_element;
		dependent_element.class_name = dependency->get_global_name();
		dependent_element.script = dependency;
		ContainerType nested_container;
		nested_container.builtin_type = Variant::ARRAY;
		nested_container.element_types.push_back(dependent_element);
		Vector<ContainerType> type_arguments;
		type_arguments.push_back(nested_container);
		const Ref<FSSpecializedClassHandle> handle =
				FSSpecializedClassHandle::create(host, type_arguments);
		Array nested_array;
		nested_array.push_back(handle);
		Dictionary nested_dictionary;
		nested_dictionary["container"] = nested_array;
		TestFSNameManglerApplicationAccessor::seed_constant(
				host, SNAME("ContainerDependency"), nested_dictionary);
		expect_rejected(host);
	}
}

TEST_CASE("[FoundryScript][NameManglerApplication] Allows non-Foundry external resources") {
	const Ref<FoundryScript> host =
			compile_bytecode_test_source("var host_value: int\n");
	Ref<Resource> external_resource;
	external_resource.instantiate();
	const String resource_path =
			"res://private_marker_external_resource.tres";
	external_resource->set_path_cache(resource_path);
	TestFSNameManglerApplicationAccessor::seed_constant(
			host, SNAME("ExternalResource"), external_resource);

	Vector<Ref<FoundryScript>> roots;
	roots.push_back(host);
	FSNameManglerApplication::Transaction transaction;
	Vector<FSNameManglerApplication::Diagnostic> diagnostics;
	REQUIRE_EQ(transaction.begin(roots, {}, diagnostics), OK);
	REQUIRE(diagnostics.is_empty());
	const Vector<uint8_t> buffer =
			name_mangler_application_serialize(host);
	CHECK(bytecode_buffer_contains(buffer, resource_path));
}

TEST_CASE("[FoundryScript][NameManglerApplication] Rejects omitted editor-cache dependencies") {
	const StringName dependency_name =
			SNAME("PrivateMarkerEditorCacheDependency");
	const Ref<FoundryScript> dependency = compile_bytecode_test_source(
			"class_name PrivateMarkerEditorCacheDependency\n");
	REQUIRE_FALSE(ScriptServer::is_global_class(dependency_name));
	ScriptServer::add_global_class(dependency_name, SNAME("RefCounted"),
			FSLanguage::get_singleton()->get_name(),
			dependency->get_script_path(), false, false, false);
	NameManglerGlobalClassRestore global_class_restore(dependency_name);

	SUBCASE("Property-list cache") {
		const Ref<FoundryScript> host = compile_bytecode_test_source(
				"var cache_probe: int\n");
		TestFSNameManglerApplicationAccessor::seed_cached_property_dependency(
				host, dependency_name);
		Vector<Ref<FoundryScript>> incomplete_roots;
		incomplete_roots.push_back(host);
		FSNameManglerApplication::Transaction transaction;
		Vector<FSNameManglerApplication::Diagnostic> diagnostics;
		CHECK_EQ(transaction.begin(incomplete_roots, {}, diagnostics),
				ERR_INVALID_PARAMETER);
		CHECK_FALSE(transaction.is_active());
		CHECK_FALSE(diagnostics.is_empty());
	}

	SUBCASE("Old static-variable cache") {
		const StringName static_member = SNAME("cache_probe");
		const Ref<FoundryScript> host = compile_bytecode_test_source(
				"static var cache_probe: int\n");
		TestFSNameManglerApplicationAccessor::seed_old_static_dependency(
				host, dependency, static_member);
		Vector<Ref<FoundryScript>> incomplete_roots;
		incomplete_roots.push_back(host);
		FSNameManglerApplication::Transaction transaction;
		Vector<FSNameManglerApplication::Diagnostic> diagnostics;
		CHECK_EQ(transaction.begin(incomplete_roots, {}, diagnostics),
				ERR_INVALID_PARAMETER);
		CHECK_FALSE(transaction.is_active());
		CHECK_FALSE(diagnostics.is_empty());
	}
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
			"\t\treturn private_marker_target_value + value\n"
			"\n"
			"func private_marker_cast(value: Variant) -> int:\n"
			"\tvar typed: PrivateMarkerApplicationTrait = value\n"
			"\treturn typed.private_marker_witness(2)\n"
			"\n"
			"@keep_name\n"
			"func run() -> int:\n"
			"\tvar value := PrivateMarkerApplicationTarget.new()\n"
			"\treturn private_marker_cast(value)\n");
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
	const Vector<FSConformanceRegistry::Conformance> parse_entries =
			FSConformanceRegistry::get_singleton()->get_file_conformances(source);
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
	REQUIRE(FSConformanceRegistry::get_singleton()->has_conformance(
			original_target, original_trait));
	FSConformanceRegistry::get_singleton()->clear_file(source);
	CHECK_FALSE(FSConformanceRegistry::get_singleton()->has_conformance(
			original_target, original_trait));
	CHECK(FSConformanceRegistry::get_singleton()->has_conformance(
			original_target, original_trait, true));
	CHECK_FALSE(target->has_script_trait_parse(original_trait));
	CHECK(target->has_script_trait(original_trait));
	FSConformanceRegistry::get_singleton()->clear_runtime_witnesses(source);
	CHECK_FALSE(FSConformanceRegistry::get_singleton()->has_conformance(
			original_target, original_trait, true));
	CHECK_FALSE(target->has_script_trait(original_trait));
	FSConformanceRegistry::get_singleton()->register_runtime_witnesses(
			source, saved);
	CHECK(target->has_script_trait(original_trait));
	FSConformanceRegistry::get_singleton()->register_file_conformances(
			source, parse_entries);
	REQUIRE(FSConformanceRegistry::get_singleton()->has_conformance(
			original_target, original_trait));
	CHECK_EQ(name_mangler_application_run(script, SNAME("run")), 42);

	const String runtime_membership_source =
			source + "::runtime-membership-probe";
	NameManglerRegistryRestore runtime_membership_restore(
			runtime_membership_source,
			Vector<FSConformanceRegistry::RuntimeConformance>());
	Vector<FSConformanceRegistry::RuntimeConformance>
			runtime_membership_entries;
	FSConformanceRegistry::RuntimeConformance native_membership;
	native_membership.target_keys.push_back("Node");
	native_membership.trait_name = original_trait;
	runtime_membership_entries.push_back(native_membership);
	FSConformanceRegistry::RuntimeConformance builtin_membership;
	builtin_membership.target_keys.push_back("int");
	builtin_membership.trait_name = original_trait;
	runtime_membership_entries.push_back(builtin_membership);
	FSConformanceRegistry::get_singleton()->register_runtime_witnesses(
			runtime_membership_source, runtime_membership_entries);
	CHECK_FALSE(
			FSConformanceRegistry::get_singleton()->native_class_conforms(
					SNAME("Node2D"), original_trait));
	CHECK(FSConformanceRegistry::get_singleton()->native_class_conforms(
			SNAME("Node2D"), original_trait, true));
	CHECK_FALSE(
			FSConformanceRegistry::get_singleton()->builtin_type_conforms(
					Variant::INT, original_trait));
	CHECK(FSConformanceRegistry::get_singleton()->builtin_type_conforms(
			Variant::INT, original_trait, true));
	FSConformanceRegistry::get_singleton()->clear_runtime_witnesses(
			runtime_membership_source);
	CHECK_FALSE(FSConformanceRegistry::get_singleton()->native_class_conforms(
			SNAME("Node2D"), original_trait, true));
	CHECK_FALSE(FSConformanceRegistry::get_singleton()->builtin_type_conforms(
			Variant::INT, original_trait, true));

	FSNameManglerAnalysis::Input input;
	input.scripts.push_back(script);
	const FSNameManglerAnalysis::Result analysis = FSNameManglerAnalysis::analyze(input);
	REQUIRE_EQ(analysis.error, OK);
	REQUIRE(analysis.rename_map.has(SNAME("PrivateMarkerApplicationTrait")));
	REQUIRE(analysis.rename_map.has(SNAME("PrivateMarkerApplicationTarget")));
	REQUIRE(analysis.rename_map.has(original_method));
	REQUIRE(analysis.rename_map.has(SNAME("private_marker_cast")));
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
	CHECK(FSConformanceRegistry::get_singleton()->has_conformance(
			transformed_target, transformed_trait));
	CHECK_FALSE(FSConformanceRegistry::get_singleton()->has_conformance(
			original_target, original_trait));
	CHECK_EQ(name_mangler_application_run(script, SNAME("run")), 42);
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
	CHECK(FSConformanceRegistry::get_singleton()->has_conformance(
			original_target, original_trait));
	CHECK_EQ(name_mangler_application_run(script, SNAME("run")), 42);
	CHECK_EQ(name_mangler_application_serialize(script), baseline);
}

TEST_CASE("[FoundryScript][NameManglerApplication] Correlates runtime conformances before staging") {
	const Ref<FoundryScript> script = compile_bytecode_test_source(
			"trait PrivateMarkerCorrelationTraitA:\n"
			"\tabstract func private_marker_correlated() -> int\n"
			"\n"
			"trait PrivateMarkerCorrelationTraitB:\n"
			"\tabstract func private_marker_correlated() -> int\n"
			"\n"
			"class PrivateMarkerCorrelationTarget:\n"
			"\tpass\n"
			"\n"
			"extend PrivateMarkerCorrelationTarget uses "
			"PrivateMarkerCorrelationTraitA, "
			"PrivateMarkerCorrelationTraitB:\n"
			"\tfunc private_marker_correlated() -> int:\n"
			"\t\treturn 42\n");
	const String source = script->get_script_path();
	FSConformanceRegistry *registry =
			FSConformanceRegistry::get_singleton();
	const Vector<FSConformanceRegistry::Conformance> parse_entries =
			registry->get_file_conformances(source);
	const Vector<FSConformanceRegistry::RuntimeConformance> runtime_entries =
			registry->get_runtime_witnesses(source);
	NameManglerRegistryRestore registry_restore(source, runtime_entries);
	REQUIRE_EQ(parse_entries.size(), 2);
	REQUIRE_EQ(runtime_entries.size(), 1);
	REQUIRE_EQ(runtime_entries[0].trait_name, StringName());
	REQUIRE_EQ(runtime_entries[0].functions.size(), 1);

	FSNameManglerAnalysis::Input input;
	input.scripts.push_back(script);
	const FSNameManglerAnalysis::Result analysis =
			FSNameManglerAnalysis::analyze(input);
	REQUIRE_EQ(analysis.error, OK);

	const Ref<FoundryScript> target =
			script->get_subclasses()[SNAME(
					"PrivateMarkerCorrelationTarget")];
	const Ref<FoundryScript> trait_a =
			script->get_subclasses()[SNAME(
					"PrivateMarkerCorrelationTraitA")];
	const Ref<FoundryScript> trait_b =
			script->get_subclasses()[SNAME(
					"PrivateMarkerCorrelationTraitB")];
	const String original_target = target->get_fully_qualified_name();
	const StringName original_trait_a = trait_a->get_trait_type_name();
	const StringName original_trait_b = trait_b->get_trait_type_name();
	Vector<uint8_t> first_staged_buffer;

	{
		FSNameManglerApplication::Transaction transaction;
		Vector<FSNameManglerApplication::Diagnostic> diagnostics;
		REQUIRE_EQ(transaction.begin(
						   input.scripts, analysis.rename_map, diagnostics),
				OK);
		REQUIRE(diagnostics.is_empty());
		const Vector<FSConformanceRegistry::RuntimeConformance> staged =
				registry->get_runtime_witnesses(source);
		REQUIRE_EQ(staged.size(), 2);
		REQUIRE_EQ(staged.size(), parse_entries.size());
		HashSet<StringName> staged_traits;
		for (int i = 0; i < staged.size(); i++) {
			const FSConformanceRegistry::RuntimeConformance &entry =
					staged[i];
			REQUIRE_EQ(entry.functions.size(), 1);
			staged_traits.insert(entry.trait_name);
			const StringName expected_trait =
					parse_entries[i].trait_name == original_trait_a
					? trait_a->get_trait_type_name()
					: trait_b->get_trait_type_name();
			const bool known_parse_trait =
					parse_entries[i].trait_name == original_trait_a ||
					parse_entries[i].trait_name == original_trait_b;
			CHECK(known_parse_trait);
			CHECK_EQ(entry.trait_name, expected_trait);
		}
		CHECK(staged_traits.has(trait_a->get_trait_type_name()));
		CHECK(staged_traits.has(trait_b->get_trait_type_name()));
		CHECK(registry->has_conformance(
				target->get_fully_qualified_name(),
				trait_a->get_trait_type_name()));
		CHECK(registry->has_conformance(
				target->get_fully_qualified_name(),
				trait_b->get_trait_type_name()));
		first_staged_buffer =
				name_mangler_application_serialize(script);
	}

	Vector<uint8_t> repeated_staged_buffer;
	{
		FSNameManglerApplication::Transaction transaction;
		Vector<FSNameManglerApplication::Diagnostic> diagnostics;
		REQUIRE_EQ(transaction.begin(
						   input.scripts, analysis.rename_map, diagnostics),
				OK);
		REQUIRE(diagnostics.is_empty());
		repeated_staged_buffer =
				name_mangler_application_serialize(script);
	}
	CHECK_EQ(repeated_staged_buffer, first_staged_buffer);

	const Vector<FSConformanceRegistry::RuntimeConformance>
			restored_runtime = registry->get_runtime_witnesses(source);
	REQUIRE_EQ(restored_runtime.size(), runtime_entries.size());
	CHECK_EQ(restored_runtime[0].trait_name,
			runtime_entries[0].trait_name);
	CHECK_EQ(restored_runtime[0].target_keys,
			runtime_entries[0].target_keys);
	REQUIRE_EQ(restored_runtime[0].functions.size(),
			runtime_entries[0].functions.size());
	for (const KeyValue<StringName, FSFunction *> &function :
			runtime_entries[0].functions) {
		const FSFunction *const *actual =
				restored_runtime[0].functions.getptr(function.key);
		REQUIRE(actual != nullptr);
		CHECK_EQ(*actual, function.value);
	}
	const Vector<FSConformanceRegistry::Conformance> restored_parse =
			registry->get_file_conformances(source);
	REQUIRE_EQ(restored_parse.size(), parse_entries.size());
	for (int i = 0; i < parse_entries.size(); i++) {
		CHECK_EQ(restored_parse[i].target_keys,
				parse_entries[i].target_keys);
		CHECK_EQ(restored_parse[i].trait_name,
				parse_entries[i].trait_name);
		REQUIRE_EQ(restored_parse[i].witnesses.size(),
				parse_entries[i].witnesses.size());
		for (const KeyValue<StringName, FSParser::FunctionNode *> &witness :
				parse_entries[i].witnesses) {
			const FSParser::FunctionNode *const *actual =
					restored_parse[i].witnesses.getptr(witness.key);
			REQUIRE(actual != nullptr);
			CHECK_EQ(*actual, witness.value);
		}
	}
	CHECK_EQ(target->get_fully_qualified_name(), original_target);
	CHECK_EQ(trait_a->get_trait_type_name(), original_trait_a);
	CHECK_EQ(trait_b->get_trait_type_name(), original_trait_b);

	const String loaded_path = source + ".correlation.fsb";
	NameManglerRegistryRestore loaded_registry_restore(
			loaded_path,
			Vector<FSConformanceRegistry::RuntimeConformance>());
	BytecodeTestResolver resolver;
	const Ref<FoundryScript> loaded = name_mangler_application_load(
			first_staged_buffer, loaded_path, &resolver);
	const Vector<FSConformanceRegistry::RuntimeConformance>
			loaded_runtime =
					registry->get_runtime_witnesses(loaded_path);
	REQUIRE_EQ(loaded_runtime.size(), 2);
	const StringName transformed_target_name =
			analysis.rename_map[SNAME("PrivateMarkerCorrelationTarget")];
	const StringName transformed_trait_a_name =
			analysis.rename_map[SNAME("PrivateMarkerCorrelationTraitA")];
	const StringName transformed_trait_b_name =
			analysis.rename_map[SNAME("PrivateMarkerCorrelationTraitB")];
	REQUIRE(loaded->get_subclasses().has(transformed_target_name));
	REQUIRE(loaded->get_subclasses().has(transformed_trait_a_name));
	REQUIRE(loaded->get_subclasses().has(transformed_trait_b_name));
	const Ref<FoundryScript> loaded_target =
			loaded->get_subclasses()[transformed_target_name];
	const Ref<FoundryScript> loaded_trait_a =
			loaded->get_subclasses()[transformed_trait_a_name];
	const Ref<FoundryScript> loaded_trait_b =
			loaded->get_subclasses()[transformed_trait_b_name];
	CHECK_FALSE(loaded_target->has_script_trait_parse(
			loaded_trait_a->get_trait_type_name()));
	CHECK_FALSE(loaded_target->has_script_trait_parse(
			loaded_trait_b->get_trait_type_name()));
	CHECK(loaded_target->has_script_trait(
			loaded_trait_a->get_trait_type_name()));
	CHECK(loaded_target->has_script_trait(
			loaded_trait_b->get_trait_type_name()));

	const auto expect_correlation_rejected =
			[&](const String &p_label,
					const Vector<FSConformanceRegistry::RuntimeConformance>
							&p_entries) {
				CAPTURE(p_label);
				registry->register_runtime_witnesses(source, p_entries);
				FSNameManglerApplication::Transaction transaction;
				Vector<FSNameManglerApplication::Diagnostic> diagnostics;
				CHECK_EQ(transaction.begin(
								 input.scripts, analysis.rename_map,
								 diagnostics),
						ERR_INVALID_PARAMETER);
				CHECK_FALSE(transaction.is_active());
				bool identifies_correlation = false;
				for (const FSNameManglerApplication::Diagnostic &diagnostic :
						diagnostics) {
					if (diagnostic.format().contains(
								"Runtime/parse conformance correlation")) {
						identifies_correlation = true;
						break;
					}
				}
				CHECK(identifies_correlation);
				CHECK_EQ(target->get_fully_qualified_name(),
						original_target);
				registry->register_runtime_witnesses(
						source, runtime_entries);
			};

	Vector<FSConformanceRegistry::RuntimeConformance> duplicated =
			runtime_entries;
	duplicated.push_back(runtime_entries[0]);
	expect_correlation_rejected("duplicate runtime entry", duplicated);

	Vector<FSConformanceRegistry::RuntimeConformance> malformed_keys =
			runtime_entries;
	int distinct_key = -1;
	for (int i = 1;
			i < malformed_keys[0].target_keys.size(); i++) {
		if (malformed_keys[0].target_keys[i] !=
				malformed_keys[0].target_keys[0]) {
			distinct_key = i;
			break;
		}
	}
	REQUIRE_GE(distinct_key, 0);
	malformed_keys.write[0].target_keys.write[distinct_key] =
			malformed_keys[0].target_keys[0];
	expect_correlation_rejected(
			"duplicate and missing target alias", malformed_keys);

	Vector<FSConformanceRegistry::RuntimeConformance> unmatched_runtime =
			runtime_entries;
	unmatched_runtime.write[0].trait_name =
			SNAME("PrivateMarkerCorrelationTarget");
	expect_correlation_rejected(
			"unmatched runtime entry", unmatched_runtime);

	Vector<FSConformanceRegistry::RuntimeConformance> unmatched_parse =
			runtime_entries;
	unmatched_parse.write[0].trait_name = original_trait_a;
	expect_correlation_rejected(
			"unmatched witness-bearing parse entry", unmatched_parse);
}

TEST_CASE("[FoundryScript][NameManglerApplication] Rejects collisions and lifecycle misuse atomically") {
	const Ref<FoundryScript> script = compile_bytecode_test_source(
			"class_name PrivateMarkerCollisionRoot\n"
			"\n"
			"@export var public_marker_existing: int = 8\n"
			"var private_marker_first: int = 1\n"
			"var private_marker_second: int = 2\n"
			"\n"
			"class PrivateMarkerCollisionOne:\n"
			"\tpass\n"
			"class PrivateMarkerCollisionTwo:\n"
			"\tpass\n"
			"\n"
			"func private_marker_first_method() -> int:\n"
			"\treturn private_marker_first\n"
			"func private_marker_second_method() -> int:\n"
			"\treturn private_marker_second\n");
	FSNameManglerAnalysis::Input input;
	input.scripts.push_back(script);
	const FSNameManglerAnalysis::Result analysis = FSNameManglerAnalysis::analyze(input);
	REQUIRE_EQ(analysis.error, OK);
	const StringName first = SNAME("private_marker_first");
	const StringName second = SNAME("private_marker_second");
	REQUIRE(analysis.rename_map.has(first));
	REQUIRE(analysis.rename_map.has(second));
	REQUIRE(analysis.rename_map.has(SNAME("PrivateMarkerCollisionOne")));
	const Vector<uint8_t> baseline = name_mangler_application_serialize(script);

	const auto check_invalid_map =
			[&](const String &p_label,
					const RBMap<StringName, StringName> &p_invalid_map) {
				CAPTURE(p_label);
				FSNameManglerApplication::Transaction transaction;
				Vector<FSNameManglerApplication::Diagnostic> diagnostics;
				const Error error =
						transaction.begin(input.scripts, p_invalid_map, diagnostics);
				CHECK_EQ(error, ERR_INVALID_PARAMETER);
				CHECK_FALSE(transaction.is_active());
				CHECK_EQ(transaction.get_state(),
						FSNameManglerApplication::Transaction::STATE_FINISHED);
				CHECK_FALSE(diagnostics.is_empty());
				if (transaction.is_active()) {
					transaction.rollback();
				}
				CHECK_EQ(name_mangler_application_serialize(script), baseline);
			};

	{
		RBMap<StringName, StringName> invalid = analysis.rename_map;
		invalid.insert(StringName(), SNAME("_fsb_manual_empty"));
		check_invalid_map("empty source", invalid);
	}
	{
		RBMap<StringName, StringName> invalid = analysis.rename_map;
		invalid.insert(SNAME("missing.source"), SNAME("_fsb_manual_composite"));
		check_invalid_map("composite source", invalid);
	}
	{
		RBMap<StringName, StringName> invalid = analysis.rename_map;
		invalid.insert(first, SNAME("invalid.replacement"));
		check_invalid_map("composite replacement", invalid);
	}
	{
		RBMap<StringName, StringName> invalid = analysis.rename_map;
		invalid.insert(first, first);
		check_invalid_map("identity mapping", invalid);
	}
	{
		RBMap<StringName, StringName> invalid = analysis.rename_map;
		invalid.insert(SNAME("missing_marker_source"),
				SNAME("_fsb_manual_missing"));
		check_invalid_map("missing source", invalid);
	}
	{
		RBMap<StringName, StringName> invalid = analysis.rename_map;
		invalid.insert(first, SNAME("_fsb_manual_duplicate"));
		invalid.insert(second, SNAME("_fsb_manual_duplicate"));
		check_invalid_map("duplicate target", invalid);
	}
	{
		RBMap<StringName, StringName> invalid = analysis.rename_map;
		invalid.insert(first, SNAME("public_marker_existing"));
		check_invalid_map("observed target", invalid);
	}
	{
		RBMap<StringName, StringName> invalid = analysis.rename_map;
		invalid.insert(SNAME("PrivateMarkerCollisionOne"),
				SNAME("PrivateMarkerCollisionTwo"));
		check_invalid_map("sibling class collision", invalid);
	}

	const auto check_invalid_roots =
			[&](const String &p_label,
					const Vector<Ref<FoundryScript>> &p_invalid_roots) {
				CAPTURE(p_label);
				FSNameManglerApplication::Transaction transaction;
				Vector<FSNameManglerApplication::Diagnostic> diagnostics;
				CHECK_EQ(transaction.begin(
								 p_invalid_roots, analysis.rename_map, diagnostics),
						ERR_INVALID_PARAMETER);
				CHECK_FALSE(transaction.is_active());
				CHECK_EQ(transaction.get_state(),
						FSNameManglerApplication::Transaction::STATE_FINISHED);
				CHECK_FALSE(diagnostics.is_empty());
				CHECK_EQ(name_mangler_application_serialize(script), baseline);
			};
	check_invalid_roots("empty roots", {});
	{
		Vector<Ref<FoundryScript>> roots;
		roots.push_back(Ref<FoundryScript>());
		check_invalid_roots("null root", roots);
	}
	{
		Vector<Ref<FoundryScript>> roots;
		roots.push_back(script);
		roots.push_back(script);
		check_invalid_roots("duplicate root", roots);
	}
	{
		Vector<Ref<FoundryScript>> roots;
		roots.push_back(
				script->get_subclasses()[SNAME("PrivateMarkerCollisionOne")]);
		check_invalid_roots("nested root", roots);
	}

	{
		FSNameManglerApplication::Transaction transaction;
		Vector<FSNameManglerApplication::Diagnostic> diagnostics;
		REQUIRE_EQ(transaction.begin(
						   input.scripts, analysis.rename_map, diagnostics),
				OK);
		CHECK_EQ(transaction.begin(input.scripts, analysis.rename_map, diagnostics),
				ERR_ALREADY_IN_USE);
		CHECK(transaction.is_active());

		FSNameManglerApplication::Transaction nested_transaction;
		CHECK_EQ(nested_transaction.begin(
						 input.scripts, analysis.rename_map, diagnostics),
				ERR_ALREADY_IN_USE);
		CHECK_FALSE(nested_transaction.is_active());

		transaction.rollback();
		transaction.rollback();
		CHECK_FALSE(transaction.is_active());
		CHECK_EQ(transaction.get_state(),
				FSNameManglerApplication::Transaction::STATE_FINISHED);
	}
	CHECK_EQ(name_mangler_application_serialize(script), baseline);

	{
		FSNameManglerApplication::Transaction transaction;
		Vector<FSNameManglerApplication::Diagnostic> diagnostics;
		REQUIRE_EQ(transaction.begin(
						   input.scripts, analysis.rename_map, diagnostics),
				OK);
	}
	CHECK_EQ(name_mangler_application_serialize(script), baseline);
}

TEST_CASE("[FoundryScript][NameManglerApplication] Real generic trait graph keeps runtime and bytecode parity") {
	static int fixture_index = 0;
	const int current_fixture = fixture_index++;
	const String base_path = TestUtils::get_temp_path(vformat(
			"name_mangler_application_graph_base_%d.fs", current_fixture));
	const String direct_trait_path = TestUtils::get_temp_path(vformat(
			"name_mangler_application_graph_direct_trait_%d.fs",
			current_fixture));
	const String conformance_trait_path = TestUtils::get_temp_path(vformat(
			"name_mangler_application_graph_conformance_trait_%d.fs",
			current_fixture));
	const String derived_path = TestUtils::get_temp_path(vformat(
			"name_mangler_application_graph_derived_%d.fs",
			current_fixture));
	const String conformance_path = TestUtils::get_temp_path(vformat(
			"name_mangler_application_graph_conformance_%d.fs",
			current_fixture));
	const String caller_path = TestUtils::get_temp_path(vformat(
			"name_mangler_application_graph_caller_%d.fs",
			current_fixture));

	NameManglerMultiFileRestore restore;
	const String paths[] = {
		base_path,
		direct_trait_path,
		conformance_trait_path,
		derived_path,
		conformance_path,
		caller_path,
	};
	for (const String &path : paths) {
		restore.track_path(path);
	}
	restore.track_conformance_source(conformance_path);

	name_mangler_application_write_source(
			base_path,
			"class_name PrivateMarkerGraphBase[T]\n"
			"extends RefCounted\n"
			"\n"
			"signal private_marker_changed(value: T)\n"
			"var private_marker_value: T\n"
			"\n"
			"enum PrivateMarkerMode:\n"
			"\tREADY = 1\n"
			"\n"
			"\tfunc private_marker_enum_identity() -> Self:\n"
			"\t\treturn self\n"
			"\n"
			"\tstatic func private_marker_enum_ready() -> Self:\n"
			"\t\treturn READY\n"
			"\n"
			"func private_marker_enum_value() -> int:\n"
			"\tvar mode: PrivateMarkerMode = "
			"PrivateMarkerMode.private_marker_enum_ready()\n"
			"\tmode = mode.private_marker_enum_identity()\n"
			"\treturn int(mode)\n"
			"\n"
			"@keep_name\n"
			"func enum_probe() -> int:\n"
			"\treturn private_marker_enum_value()\n"
			"\n"
			"func private_marker_store(value: T) -> T:\n"
			"\tprivate_marker_value = value\n"
			"\tprivate_marker_changed.emit(value)\n"
			"\treturn private_marker_value\n");
	name_mangler_application_write_source(
			direct_trait_path,
			"trait_name PrivateMarkerGraphDirectTrait\n"
			"\n"
			"abstract func private_marker_direct_bonus(value: int) -> int\n");
	name_mangler_application_write_source(
			conformance_trait_path,
			"trait_name PrivateMarkerGraphConformanceTrait\n"
			"\n"
			"abstract func private_marker_trait_value(value: int) -> int\n");
	name_mangler_application_write_source(
			derived_path,
			vformat(
					"class_name PrivateMarkerGraphDerived\n"
					"extends \"%s\"[int]\n"
					"uses PrivateMarkerGraphDirectTrait\n"
					"\n"
					"func private_marker_direct_bonus(value: int) -> int:\n"
					"\treturn value + 1\n"
					"\n"
					"func private_marker_run() -> int:\n"
					"\treturn private_marker_store(40) + "
					"private_marker_direct_bonus(1)\n",
					base_path));
	name_mangler_application_write_source(
			conformance_path,
			"extend PrivateMarkerGraphDerived uses "
			"PrivateMarkerGraphConformanceTrait:\n"
			"\tfunc private_marker_trait_value(value: int) -> int:\n"
			"\t\treturn private_marker_value + value\n");
	name_mangler_application_write_source(
			caller_path,
			vformat(
					"const PrivateMarkerDerived = preload(\"%s\")\n"
					"const PrivateMarkerConformance = preload(\"%s\")\n"
					"\n"
					"@keep_name\n"
					"func run() -> int:\n"
					"\tvar instance := PrivateMarkerDerived.new()\n"
					"\tvar direct := instance.private_marker_run()\n"
					"\tvar widened: Object = instance\n"
					"\tif not widened is "
					"PrivateMarkerGraphConformanceTrait:\n"
					"\t\treturn -1\n"
					"\tvar typed := widened as "
					"PrivateMarkerGraphConformanceTrait\n"
					"\treturn direct + typed.private_marker_trait_value(2)\n",
					derived_path, conformance_path));

	restore.register_global_class(
			SNAME("PrivateMarkerGraphBase"), SNAME("RefCounted"),
			base_path);
	restore.register_global_class(
			SNAME("PrivateMarkerGraphDirectTrait"), SNAME("RefCounted"),
			direct_trait_path, true);
	restore.register_global_class(
			SNAME("PrivateMarkerGraphConformanceTrait"), SNAME("RefCounted"),
			conformance_trait_path, true);
	restore.register_global_class(
			SNAME("PrivateMarkerGraphDerived"),
			SNAME("PrivateMarkerGraphBase"), derived_path);

	const Ref<FoundryScript> base =
			name_mangler_application_load_source(base_path);
	const Ref<FoundryScript> direct_trait =
			name_mangler_application_load_source(direct_trait_path);
	const Ref<FoundryScript> conformance_trait =
			name_mangler_application_load_source(conformance_trait_path);
	const Ref<FoundryScript> derived =
			name_mangler_application_load_source(derived_path);
	const Ref<FoundryScript> conformance =
			name_mangler_application_load_source(conformance_path);
	const Ref<FoundryScript> caller =
			name_mangler_application_load_source(caller_path);
	restore.track_script(base);
	restore.track_script(direct_trait);
	restore.track_script(conformance_trait);
	restore.track_script(derived);
	restore.track_script(conformance);
	restore.track_script(caller);

	Vector<Ref<FoundryScript>> roots;
	roots.push_back(base);
	roots.push_back(direct_trait);
	roots.push_back(conformance_trait);
	roots.push_back(derived);
	roots.push_back(conformance);
	roots.push_back(caller);
	FSNameManglerAnalysis::Input input;
	input.scripts = roots;
	const FSNameManglerAnalysis::Result analysis =
			FSNameManglerAnalysis::analyze(input);
	REQUIRE_EQ(analysis.error, OK);
	const StringName private_names[] = {
		SNAME("PrivateMarkerGraphBase"),
		SNAME("PrivateMarkerGraphDirectTrait"),
		SNAME("PrivateMarkerGraphConformanceTrait"),
		SNAME("PrivateMarkerGraphDerived"),
		SNAME("PrivateMarkerMode"),
		SNAME("private_marker_changed"),
		SNAME("private_marker_value"),
		SNAME("private_marker_enum_identity"),
		SNAME("private_marker_enum_ready"),
		SNAME("private_marker_enum_value"),
		SNAME("private_marker_store"),
		SNAME("private_marker_direct_bonus"),
		SNAME("private_marker_run"),
		SNAME("private_marker_trait_value"),
		SNAME("PrivateMarkerDerived"),
		SNAME("PrivateMarkerConformance"),
	};
	for (const StringName &name : private_names) {
		REQUIRE(analysis.rename_map.has(name));
	}
	REQUIRE_FALSE(analysis.rename_map.has(SNAME("run")));
	REQUIRE_FALSE(analysis.rename_map.has(SNAME("enum_probe")));
	CHECK_EQ(name_mangler_application_run(base, SNAME("enum_probe")), 1);
	CHECK_EQ(name_mangler_application_run(caller, SNAME("run")), 84);

	FSFunction *const original_enum_instance = base->get_enum_function(
			SNAME("PrivateMarkerMode"),
			SNAME("private_marker_enum_identity"), false);
	FSFunction *const original_enum_static = base->get_enum_function(
			SNAME("PrivateMarkerMode"),
			SNAME("private_marker_enum_ready"), true);
	REQUIRE(original_enum_instance != nullptr);
	REQUIRE(original_enum_static != nullptr);
	const String original_instance_return =
			original_enum_instance->get_method_info().return_val.class_name;
	const String original_static_return =
			original_enum_static->get_method_info().return_val.class_name;
	REQUIRE(original_instance_return.contains("PrivateMarkerMode"));
	REQUIRE(original_static_return.contains("PrivateMarkerMode"));

	Vector<Vector<uint8_t>> first_buffers;
	Vector<String> transformed_identities;
	{
		FSNameManglerApplication::Transaction transaction;
		Vector<FSNameManglerApplication::Diagnostic> diagnostics;
		REQUIRE_EQ(transaction.begin(
						   roots, analysis.rename_map, diagnostics),
				OK);
		REQUIRE(diagnostics.is_empty());
		CHECK_EQ(name_mangler_application_run(base, SNAME("enum_probe")), 1);
		CHECK_EQ(name_mangler_application_run(caller, SNAME("run")), 84);

		const StringName transformed_enum =
				analysis.rename_map[SNAME("PrivateMarkerMode")];
		const StringName transformed_enum_instance =
				analysis.rename_map[SNAME("private_marker_enum_identity")];
		const StringName transformed_enum_static =
				analysis.rename_map[SNAME("private_marker_enum_ready")];
		FSFunction *const staged_enum_instance = base->get_enum_function(
				transformed_enum, transformed_enum_instance, false);
		FSFunction *const staged_enum_static = base->get_enum_function(
				transformed_enum, transformed_enum_static, true);
		REQUIRE(staged_enum_instance != nullptr);
		REQUIRE(staged_enum_static != nullptr);
		const String staged_instance_return =
				staged_enum_instance->get_method_info().return_val.class_name;
		const String staged_static_return =
				staged_enum_static->get_method_info().return_val.class_name;
		CHECK_FALSE(staged_instance_return.contains("PrivateMarkerMode"));
		CHECK_FALSE(staged_static_return.contains("PrivateMarkerMode"));
		CHECK(staged_instance_return.contains(String(transformed_enum)));
		CHECK(staged_static_return.contains(String(transformed_enum)));

		for (const Ref<FoundryScript> &root_script : roots) {
			transformed_identities.push_back(
					root_script->get_fully_qualified_name());
			first_buffers.push_back(
					name_mangler_application_serialize(root_script));
		}
		for (const Vector<uint8_t> &buffer : first_buffers) {
			for (const StringName &name : private_names) {
				CHECK_FALSE(
						bytecode_buffer_contains(buffer, String(name)));
			}
		}
		CHECK(bytecode_buffer_contains(first_buffers[3], base_path));
		CHECK(bytecode_buffer_contains(first_buffers[5], derived_path));
		CHECK(bytecode_buffer_contains(first_buffers[5], conformance_path));
		CHECK(bytecode_buffer_contains(first_buffers[5], "run"));
		CHECK(bytecode_buffer_contains(first_buffers[0], "RefCounted"));
		transaction.rollback();
	}
	CHECK_EQ(name_mangler_application_run(base, SNAME("enum_probe")), 1);
	CHECK_EQ(name_mangler_application_run(caller, SNAME("run")), 84);
	CHECK_EQ(
			original_enum_instance->get_method_info().return_val.class_name,
			original_instance_return);
	CHECK_EQ(original_enum_static->get_method_info().return_val.class_name,
			original_static_return);

	Vector<Ref<FoundryScript>> reversed_roots;
	for (int i = roots.size() - 1; i >= 0; i--) {
		reversed_roots.push_back(roots[i]);
	}
	Vector<Vector<uint8_t>> reversed_buffers;
	{
		FSNameManglerApplication::Transaction transaction;
		Vector<FSNameManglerApplication::Diagnostic> diagnostics;
		REQUIRE_EQ(transaction.begin(
						   reversed_roots, analysis.rename_map, diagnostics),
				OK);
		for (const Ref<FoundryScript> &root_script : roots) {
			reversed_buffers.push_back(
					name_mangler_application_serialize(root_script));
		}
	}
	REQUIRE_EQ(reversed_buffers.size(), first_buffers.size());
	for (int i = 0; i < first_buffers.size(); i++) {
		CHECK_EQ(reversed_buffers[i], first_buffers[i]);
	}

	BytecodeTestResolver resolver;
	Vector<Ref<FoundryScript>> loaded_roots;
	for (int i = 0; i < roots.size(); i++) {
		const Ref<FoundryScript> loaded = name_mangler_application_load(
				first_buffers[i], paths[i], &resolver);
		loaded_roots.push_back(loaded);
		restore.track_script(loaded);
		resolver.scripts.insert(
				paths[i] + "::" + transformed_identities[i], loaded);
	}
	CHECK_EQ(name_mangler_application_run(loaded_roots[0], SNAME("enum_probe")),
			1);
	CHECK_EQ(name_mangler_application_run(
					 loaded_roots[loaded_roots.size() - 1], SNAME("run")),
			84);
}

TEST_CASE("[FoundryScript][NameManglerApplication] Multi-file runtime and serialization are deterministic") {
	const Ref<FoundryScript> base = compile_bytecode_test_source(
			"class_name PrivateMarkerApplicationRuntimeBase\n"
			"extends RefCounted\n"
			"\n"
			"signal private_marker_changed(value: int)\n"
			"var private_marker_value: int = 40\n"
			"\n"
			"func private_marker_bump(delta: int) -> int:\n"
			"\tprivate_marker_value += delta\n"
			"\tprivate_marker_changed.emit(private_marker_value)\n"
			"\treturn private_marker_value\n");
	const Ref<FoundryScript> derived = compile_bytecode_test_source(vformat(
			"class_name PrivateMarkerApplicationRuntimeDerived\n"
			"extends \"%s\"\n"
			"\n"
			"func private_marker_run() -> int:\n"
			"\treturn private_marker_bump(2)\n",
			base->get_script_path()));
	const Ref<FoundryScript> caller = compile_bytecode_test_source(vformat(
			"class_name PrivateMarkerApplicationRuntimeCaller\n"
			"extends RefCounted\n"
			"\n"
			"const PrivateMarkerDerivedResource = preload(\"%s\")\n"
			"\n"
			"@keep_name\n"
			"func run() -> int:\n"
			"\tvar instance := PrivateMarkerDerivedResource.new()\n"
			"\treturn instance.private_marker_run()\n",
			derived->get_script_path()));

	FSNameManglerAnalysis::Input input;
	input.scripts.push_back(base);
	input.scripts.push_back(derived);
	input.scripts.push_back(caller);
	const FSNameManglerAnalysis::Result analysis = FSNameManglerAnalysis::analyze(input);
	REQUIRE_EQ(analysis.error, OK);
	const StringName private_names[] = {
		SNAME("PrivateMarkerApplicationRuntimeBase"),
		SNAME("PrivateMarkerApplicationRuntimeDerived"),
		SNAME("PrivateMarkerApplicationRuntimeCaller"),
		SNAME("private_marker_changed"),
		SNAME("private_marker_value"),
		SNAME("private_marker_bump"),
		SNAME("private_marker_run"),
		SNAME("PrivateMarkerDerivedResource"),
	};
	for (const StringName &name : private_names) {
		REQUIRE(analysis.rename_map.has(name));
	}
	REQUIRE_FALSE(analysis.rename_map.has(SNAME("run")));

	CHECK_EQ(name_mangler_application_run(caller, SNAME("run")), 42);

	Vector<Vector<uint8_t>> first_buffers;
	Vector<String> transformed_identities;
	{
		FSNameManglerApplication::Transaction transaction;
		Vector<FSNameManglerApplication::Diagnostic> diagnostics;
		REQUIRE_EQ(transaction.begin(
						   input.scripts, analysis.rename_map, diagnostics),
				OK);
		REQUIRE(diagnostics.is_empty());
		CHECK_EQ(name_mangler_application_run(caller, SNAME("run")), 42);
		transformed_identities.push_back(base->get_fully_qualified_name());
		transformed_identities.push_back(derived->get_fully_qualified_name());
		transformed_identities.push_back(caller->get_fully_qualified_name());
		first_buffers.push_back(name_mangler_application_serialize(base));
		first_buffers.push_back(name_mangler_application_serialize(derived));
		first_buffers.push_back(name_mangler_application_serialize(caller));

		for (const Vector<uint8_t> &buffer : first_buffers) {
			for (const StringName &name : private_names) {
				CHECK_FALSE(bytecode_buffer_contains(buffer, String(name)));
			}
		}
		CHECK(bytecode_buffer_contains(first_buffers[1], base->get_script_path()));
		CHECK(bytecode_buffer_contains(
				first_buffers[2], derived->get_script_path()));
		CHECK(bytecode_buffer_contains(first_buffers[2], "run"));
		CHECK(bytecode_buffer_contains(first_buffers[0], "RefCounted"));
	}
	CHECK_EQ(name_mangler_application_run(caller, SNAME("run")), 42);

	Vector<Ref<FoundryScript>> reversed_roots;
	reversed_roots.push_back(caller);
	reversed_roots.push_back(derived);
	reversed_roots.push_back(base);
	Vector<Vector<uint8_t>> reversed_buffers;
	{
		FSNameManglerApplication::Transaction transaction;
		Vector<FSNameManglerApplication::Diagnostic> diagnostics;
		REQUIRE_EQ(transaction.begin(
						   reversed_roots, analysis.rename_map, diagnostics),
				OK);
		reversed_buffers.push_back(name_mangler_application_serialize(base));
		reversed_buffers.push_back(name_mangler_application_serialize(derived));
		reversed_buffers.push_back(name_mangler_application_serialize(caller));
	}
	REQUIRE_EQ(reversed_buffers.size(), first_buffers.size());
	for (int i = 0; i < first_buffers.size(); i++) {
		CHECK_EQ(reversed_buffers[i], first_buffers[i]);
	}

	BytecodeTestResolver resolver;
	const Ref<FoundryScript> loaded_base = name_mangler_application_load(
			first_buffers[0], base->get_script_path(), &resolver);
	resolver.scripts.insert(
			base->get_script_path() + "::" + transformed_identities[0],
			loaded_base);
	const Ref<FoundryScript> loaded_derived = name_mangler_application_load(
			first_buffers[1], derived->get_script_path(), &resolver);
	resolver.scripts.insert(
			derived->get_script_path() + "::" + transformed_identities[1],
			loaded_derived);
	const Ref<FoundryScript> loaded_caller = name_mangler_application_load(
			first_buffers[2], caller->get_script_path(), &resolver);
	CHECK_EQ(name_mangler_application_run(loaded_caller, SNAME("run")), 42);
}

} // namespace FSTests

#endif // TOOLS_ENABLED
