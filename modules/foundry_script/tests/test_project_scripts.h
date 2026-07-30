/**************************************************************************/
/*  test_project_scripts.h                                                */
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

#pragma once

#ifndef FOUNDRY_SCRIPT_NO_FRONTEND

#include "../foundry_script.h"
#include "../fs_cache.h"
#include "../fs_project_scripts.h"
#include "../fs_reflection.h"
#include "fs_temporary_project_tree.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/object/script_language.h"
#include "core/string/print_string.h"
#include "tests/core/config/test_project_settings.h"
#include "tests/test_macros.h"

namespace FSTests {

static const char *discovery_fixtures_root = "modules/foundry_script/tests/scripts/project_scripts_discovery";

struct ScopedDiscoveryProject {
	String old_resource_path;
	String fixture_root;

	explicit ScopedDiscoveryProject() {
		old_resource_path = TestProjectSettingsInternalsAccessor::resource_path();
		TestProjectSettingsInternalsAccessor::resource_path() = DirAccess::create(DirAccess::ACCESS_FILESYSTEM)->get_current_dir();
		fixture_root = String("res://") + discovery_fixtures_root;
		FSLanguage::get_singleton()->init();
	}

	~ScopedDiscoveryProject() {
		FSCache::clear();
		FSLanguage::get_singleton()->finish();
		TestProjectSettingsInternalsAccessor::resource_path() = old_resource_path;
	}

	String fixture_path(const String &p_relative) const {
		return fixture_root.path_join(p_relative);
	}
};

struct ScopedPrintCapture {
	bool printed = false;
	PrintHandlerList handler;

	static void capture(void *p_userdata, const String &p_message, bool p_error, bool p_rich) {
		ScopedPrintCapture *capture_state = static_cast<ScopedPrintCapture *>(p_userdata);
		if (!p_error) {
			capture_state->printed = true;
		}
		(void)p_message;
		(void)p_rich;
	}

	ScopedPrintCapture() {
		handler.printfunc = capture;
		handler.userdata = this;
		add_print_handler(&handler);
	}

	~ScopedPrintCapture() {
		remove_print_handler(&handler);
	}
};

struct ScopedGlobalClass {
	StringName class_name;

	ScopedGlobalClass(const String &p_path) {
		String base_type;
		bool is_abstract = false;
		bool is_tool = false;
		bool is_trait = false;
		class_name = FSLanguage::get_singleton()->get_global_class_name(p_path, &base_type, nullptr, &is_abstract, &is_tool, &is_trait);
		if (!class_name.is_empty()) {
			ScriptServer::add_global_class(class_name, base_type, FSLanguage::get_singleton()->get_name(), p_path, is_abstract, is_tool, is_trait);
		}
	}

	~ScopedGlobalClass() {
		if (!class_name.is_empty()) {
			ScriptServer::remove_global_class(class_name);
		}
	}
};

static Vector<String> descriptor_paths(const TypedArray<FSScriptDescriptor> &p_descriptors) {
	Vector<String> paths;
	for (int i = 0; i < p_descriptors.size(); i++) {
		Ref<FSScriptDescriptor> descriptor = p_descriptors[i];
		paths.push_back(descriptor->get_path());
	}
	return paths;
}

TEST_CASE("[Modules][FoundryScript][ProjectScripts] list_scripts_under enumerates fixtures in lexicographic order") {
	ScopedDiscoveryProject project;
	Ref<FSProjectScripts> discovery;
	discovery.instantiate();

	SUBCASE("recursive listing includes nested scripts in lexicographic order") {
		const TypedArray<FSScriptDescriptor> descriptors = discovery->list_scripts_under(project.fixture_root, true);
		const Vector<String> paths = descriptor_paths(descriptors);
		CHECK(paths.has(project.fixture_path("a_root.notest.fs")));
		CHECK(paths.has(project.fixture_path("nested/b_nested.notest.fs")));
		CHECK(paths.has(project.fixture_path("z_root.notest.fs")));

		for (int i = 1; i < paths.size(); i++) {
			CHECK(paths[i - 1] < paths[i]);
		}
	}

	SUBCASE("non-recursive listing skips nested directories") {
		const TypedArray<FSScriptDescriptor> descriptors = discovery->list_scripts_under(project.fixture_root, false);
		const Vector<String> paths = descriptor_paths(descriptors);
		CHECK(paths.has(project.fixture_path("a_root.notest.fs")));
		CHECK(paths.has(project.fixture_path("z_root.notest.fs")));
		CHECK_FALSE(paths.has(project.fixture_path("nested/b_nested.notest.fs")));
	}
}

TEST_CASE("[Modules][FoundryScript][ProjectScripts] Descriptor metadata matches indexed fixture scripts") {
	ScopedDiscoveryProject project;
	Ref<FSProjectScripts> discovery;
	discovery.instantiate();

	const Ref<FSScriptDescriptor> descriptor = discovery->get_script_descriptor(project.fixture_path("annotations.notest.fs"));
	REQUIRE(descriptor.is_valid());
	REQUIRE(descriptor->get_indexed_ok());
	CHECK(descriptor->get_index_diagnostics().is_empty());
	CHECK_EQ(descriptor->get_global_class_name(), StringName());
	CHECK_EQ(descriptor->get_fully_qualified_name(), StringName("cafecito.discovery_demo.InventorySuite"));
	CHECK_EQ(descriptor->get_base_type(), StringName("RefCounted"));
	CHECK_FALSE(descriptor->get_is_trait());
	CHECK_FALSE(descriptor->get_is_abstract());

	const TypedArray<FSAnnotation> class_annotations = descriptor->get_class_annotations();
	REQUIRE_EQ(class_annotations.size(), 1);
	CHECK_EQ(Ref<FSAnnotation>(class_annotations[0])->get_annotation_name(), StringName("suite"));
	CHECK_EQ(String(Ref<FSAnnotation>(class_annotations[0])->get_named_arguments()["name"]), "Inventory Suite");

	const TypedArray<FSMethodDescriptor> methods = descriptor->get_methods();
	REQUIRE_EQ(methods.size(), 1);
	Ref<FSMethodDescriptor> test_method;
	for (int i = 0; i < methods.size(); i++) {
		Ref<FSMethodDescriptor> method = methods[i];
		if (method->get_method_name() == StringName("test_add_item")) {
			test_method = method;
			break;
		}
	}
	REQUIRE(test_method.is_valid());
	const TypedArray<FSAnnotation> method_annotations = test_method->get_annotations();
	REQUIRE_EQ(method_annotations.size(), 1);
	CHECK_EQ(Ref<FSAnnotation>(method_annotations[0])->get_annotation_name(), StringName("timeout"));
	CHECK_EQ(double(Ref<FSAnnotation>(method_annotations[0])->get_arguments()[0]), doctest::Approx(2.5));

	const TypedArray<Dictionary> arguments = test_method->get_arguments();
	REQUIRE_EQ(arguments.size(), 1);
	const Dictionary first_argument = arguments[0];
	CHECK(first_argument.has("annotations"));
	CHECK_EQ(Array(first_argument.get("annotations", Variant())).size(), 1);
}

TEST_CASE("[Modules][FoundryScript][ProjectScripts] implements_trait matches qualified trait identity and inherited traits") {
	ScopedDiscoveryProject project;
	ScopedGlobalClass base_global(project.fixture_path("trait_base.notest.fs"));
	Ref<FSProjectScripts> discovery;
	discovery.instantiate();

	const StringName trait_identity = StringName("InventoryBase::InventoryTestable");

	const Ref<FSScriptDescriptor> base_descriptor = discovery->get_script_descriptor(project.fixture_path("trait_base.notest.fs"));
	REQUIRE(base_descriptor.is_valid());
	REQUIRE(base_descriptor->get_indexed_ok());
	CHECK(base_descriptor->implements_trait(trait_identity));

	const Ref<FSScriptDescriptor> descriptor = discovery->get_script_descriptor(project.fixture_path("trait_suite.notest.fs"));
	REQUIRE(descriptor.is_valid());
	REQUIRE(descriptor->get_indexed_ok());
	CHECK(descriptor->implements_trait(trait_identity));
	CHECK_FALSE(descriptor->implements_trait(StringName("MissingTrait")));
}

TEST_CASE("[Modules][FoundryScript][ProjectScripts] Broken scripts surface indexing diagnostics without printing") {
	ScopedDiscoveryProject project;
	ScopedPrintCapture capture;
	Ref<FSProjectScripts> discovery;
	discovery.instantiate();

	const TypedArray<FSScriptDescriptor> descriptors = discovery->list_scripts_under(project.fixture_root, true);
	Ref<FSScriptDescriptor> broken;
	for (int i = 0; i < descriptors.size(); i++) {
		Ref<FSScriptDescriptor> descriptor = descriptors[i];
		if (descriptor->get_path().ends_with("syntax_error.notest.fs")) {
			broken = descriptor;
			break;
		}
	}

	REQUIRE(broken.is_valid());
	CHECK_FALSE(broken->get_indexed_ok());
	CHECK_FALSE(broken->get_index_diagnostics().is_empty());
	CHECK_FALSE(capture.printed);
}

TEST_CASE("[Modules][FoundryScript][ProjectScripts] Discovery does not execute static initialization or _init") {
	ScopedDiscoveryProject project;
	ScopedPrintCapture capture;
	Ref<FSProjectScripts> discovery;
	discovery.instantiate();

	const Ref<FSScriptDescriptor> descriptor = discovery->get_script_descriptor(project.fixture_path("side_effect.notest.fs"));
	REQUIRE(descriptor.is_valid());
	CHECK_FALSE(capture.printed);

	const Ref<Script> script = descriptor->load_script();
	REQUIRE(script.is_valid());
	CHECK(capture.printed);
}

TEST_CASE("[Modules][FoundryScript][ProjectScripts] load_script returns an instantiable script") {
	ScopedDiscoveryProject project;
	Ref<FSProjectScripts> discovery;
	discovery.instantiate();

	const Ref<FSScriptDescriptor> descriptor = discovery->get_script_descriptor(project.fixture_path("a_root.notest.fs"));
	REQUIRE(descriptor.is_valid());
	CHECK(descriptor->get_indexed_ok());

	const Ref<Script> script = descriptor->load_script();
	REQUIRE(script.is_valid());
	Ref<RefCounted> object = memnew(RefCounted);
	ScriptInstance *instance = script->instance_create(object.ptr());
	REQUIRE(instance != nullptr);
}

TEST_CASE("[Modules][FoundryScript][ProjectScripts] Repeated discovery reuses parser cache and stays stable") {
	ScopedDiscoveryProject project;
	Ref<FSProjectScripts> discovery;
	discovery.instantiate();

	const TypedArray<FSScriptDescriptor> first = discovery->list_scripts_under(project.fixture_root, true);
	const TypedArray<FSScriptDescriptor> second = discovery->list_scripts_under(project.fixture_root, true);
	REQUIRE_EQ(first.size(), second.size());
	for (int i = 0; i < first.size(); i++) {
		Ref<FSScriptDescriptor> first_descriptor = first[i];
		Ref<FSScriptDescriptor> second_descriptor = second[i];
		CHECK_EQ(first_descriptor->get_path(), second_descriptor->get_path());
		CHECK_EQ(first_descriptor->get_indexed_ok(), second_descriptor->get_indexed_ok());
		CHECK_EQ(first_descriptor->get_global_class_name(), second_descriptor->get_global_class_name());
	}
}

TEST_CASE("[Modules][FoundryScript][ProjectScripts] get_script_descriptor returns null for missing files") {
	ScopedDiscoveryProject project;
	Ref<FSProjectScripts> discovery;
	discovery.instantiate();
	CHECK_FALSE(discovery->get_script_descriptor(project.fixture_path("missing.fs")).is_valid());
}

TEST_CASE("[Modules][FoundryScript][ProjectScripts] foundry namespace exposes project_scripts") {
	FSLanguage::get_singleton()->init();
	Ref<FSNamespace> foundry = FSLanguage::get_singleton()->get_namespace_singleton();
	REQUIRE(foundry.is_valid());
	Ref<FSProjectScripts> project_scripts = foundry->get_project_scripts();
	CHECK(project_scripts.is_valid());
	FSLanguage::get_singleton()->finish();
}

} // namespace FSTests

#endif // FOUNDRY_SCRIPT_NO_FRONTEND
