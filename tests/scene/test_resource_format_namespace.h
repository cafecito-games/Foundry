/**************************************************************************/
/*  test_resource_format_namespace.h                                      */
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

#include "core/io/file_access.h"
#include "core/io/missing_resource.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_saver.h"
#include "core/object/class_db.h"

#include "tests/test_macros.h"
#include "tests/test_utils.h"

// Declared in the global namespace because of the `FOUNDRY_CLASS` friend-declaration warning on
// Windows when the macro is expanded inside a namespace.
class _ResourceFormatNamespacedResource : public Resource {
	FOUNDRY_CLASS(_ResourceFormatNamespacedResource, Resource);

	int value = 0;
	Ref<Resource> child;

protected:
	static void _bind_methods() {
		ClassDB::bind_method(D_METHOD("set_value", "value"), &_ResourceFormatNamespacedResource::set_value);
		ClassDB::bind_method(D_METHOD("get_value"), &_ResourceFormatNamespacedResource::get_value);
		ADD_PROPERTY(PropertyInfo(Variant::INT, "value"), "set_value", "get_value");

		ClassDB::bind_method(D_METHOD("set_child", "child"), &_ResourceFormatNamespacedResource::set_child);
		ClassDB::bind_method(D_METHOD("get_child"), &_ResourceFormatNamespacedResource::get_child);
		ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "child", PROPERTY_HINT_RESOURCE_TYPE, "Resource"), "set_child", "get_child");
	}

public:
	void set_value(int p_value) { value = p_value; }
	int get_value() const { return value; }

	void set_child(const Ref<Resource> &p_child) { child = p_child; }
	Ref<Resource> get_child() const { return child; }
};

class _ResourceFormatOtherNamespacedResource : public Resource {
	FOUNDRY_CLASS(_ResourceFormatOtherNamespacedResource, Resource);
};

namespace TestResourceFormatNamespace {

constexpr const char *NAMESPACED_RESOURCE = "foundry.test.resource._ResourceFormatNamespacedResource";
constexpr const char *OTHER_NAMESPACED_RESOURCE = "foundry.test.resource.other._ResourceFormatOtherNamespacedResource";
constexpr const char *UNIQUE_ALIAS = "_ResourceFormatUniqueAlias";
constexpr const char *AMBIGUOUS_ALIAS = "_ResourceFormatAmbiguousAlias";

// `ClassDB` cannot undo a namespace rekey or an alias registration, so the two test classes get their
// permanent registry shape once and every case below only reads it. The unique and the ambiguous
// alias are separate names so no case can poison another, in any execution order.
static void ensure_resource_registrations() {
	static bool registered = false;
	if (registered) {
		return;
	}
	registered = true;

	FOUNDRY_REGISTER_CLASS(_ResourceFormatNamespacedResource);
	FOUNDRY_REGISTER_CLASS(_ResourceFormatOtherNamespacedResource);
	FOUNDRY_REGISTER_NAMESPACE(_ResourceFormatNamespacedResource, "foundry.test.resource");
	FOUNDRY_REGISTER_NAMESPACE(_ResourceFormatOtherNamespacedResource, "foundry.test.resource.other");

	ClassDB::class_register_global_alias(NAMESPACED_RESOURCE, UNIQUE_ALIAS);
	ClassDB::class_register_global_alias(NAMESPACED_RESOURCE, AMBIGUOUS_ALIAS);
	ClassDB::class_register_global_alias(OTHER_NAMESPACED_RESOURCE, AMBIGUOUS_ALIAS);
}

// Writes `p_text` verbatim to a scratch file and returns its path.
static String write_scratch_file(const String &p_file_name, const String &p_text) {
	const String path = TestUtils::get_temp_path(p_file_name);
	Ref<FileAccess> file = FileAccess::open(path, FileAccess::WRITE);
	REQUIRE(file.is_valid());
	file->store_string(p_text);
	return path;
}

static Ref<Resource> load_uncached(const String &p_path) {
	Error error = FAILED;
	Ref<Resource> loaded = ResourceLoader::load(p_path, "", ResourceFormatLoader::CACHE_MODE_IGNORE, &error);
	CHECK_EQ(error, OK);
	return loaded;
}

// Turns unresolvable resource types into a recording `MissingResource` for the lifetime of the scope,
// which is how the tests below observe the on-disk type string the loader could not resolve.
class MissingResourceRecordingScope {
	bool previous = false;

public:
	MissingResourceRecordingScope() :
			previous(ResourceLoader::is_creating_missing_resources_if_class_unavailable_enabled()) {
		ResourceLoader::set_create_missing_resources_if_class_unavailable(true);
	}
	~MissingResourceRecordingScope() {
		ResourceLoader::set_create_missing_resources_if_class_unavailable(previous);
	}
};

TEST_CASE("[ResourceFormatNamespace] A text resource round-trips its qualified type") {
	ensure_resource_registrations();

	const String path = write_scratch_file("resource_format_qualified.tres",
			vformat("[gd_resource type=\"%s\" format=3]\n\n[resource]\nvalue = 11\n", NAMESPACED_RESOURCE));

	Ref<Resource> loaded = load_uncached(path);
	REQUIRE(loaded.is_valid());
	CHECK_EQ(loaded->get_class(), String(NAMESPACED_RESOURCE));
	CHECK_EQ(int(loaded->get("value")), 11);

	// Saving writes the same qualified string back, and it loads again unchanged.
	const String resaved_path = TestUtils::get_temp_path("resource_format_qualified_resaved.tres");
	REQUIRE_EQ(ResourceSaver::save(loaded, resaved_path), OK);
	Ref<Resource> reloaded = load_uncached(resaved_path);
	REQUIRE(reloaded.is_valid());
	CHECK_EQ(reloaded->get_class(), String(NAMESPACED_RESOURCE));
	CHECK_EQ(int(reloaded->get("value")), 11);
}

TEST_CASE("[ResourceFormatNamespace] A text sub-resource resolves its qualified type") {
	ensure_resource_registrations();

	const String path = write_scratch_file("resource_format_qualified_sub.tres",
			vformat("[gd_resource type=\"%s\" load_steps=2 format=3]\n\n"
					"[sub_resource type=\"%s\" id=\"Child_1\"]\nvalue = 5\n\n"
					"[resource]\nvalue = 9\nchild = SubResource(\"Child_1\")\n",
					NAMESPACED_RESOURCE, NAMESPACED_RESOURCE));

	Ref<Resource> loaded = load_uncached(path);
	REQUIRE(loaded.is_valid());
	CHECK_EQ(loaded->get_class(), String(NAMESPACED_RESOURCE));
	CHECK_EQ(int(loaded->get("value")), 9);

	Ref<Resource> child = loaded->get("child");
	REQUIRE(child.is_valid());
	CHECK_EQ(child->get_class(), String(NAMESPACED_RESOURCE));
	CHECK_EQ(int(child->get("value")), 5);
}

// The scene unique id a saver derives for a built-in resource must be a valid identifier, so a
// namespaced class contributes only the segment after its last namespace separator.
static void check_derived_scene_unique_id(const Ref<Resource> &p_resource, const String &p_qualified_class) {
	const String simple_name = p_qualified_class.substr(p_qualified_class.rfind_char('.') + 1);
	const String id = p_resource->get_scene_unique_id();
	CHECK(id.begins_with(simple_name + "_"));
	CHECK_FALSE(id.contains("."));
}

TEST_CASE("[ResourceFormatNamespace] A binary resource round-trips its qualified type") {
	ensure_resource_registrations();

	Ref<Resource> source = Ref<Resource>(Object::cast_to<Resource>(ClassDB::instantiate(NAMESPACED_RESOURCE)));
	REQUIRE(source.is_valid());
	source->set("value", 23);

	const String path = TestUtils::get_temp_path("resource_format_qualified.res");
	REQUIRE_EQ(ResourceSaver::save(source, path), OK);
	check_derived_scene_unique_id(source, NAMESPACED_RESOURCE);

	Ref<Resource> loaded = load_uncached(path);
	REQUIRE(loaded.is_valid());
	CHECK_EQ(loaded->get_class(), String(NAMESPACED_RESOURCE));
	CHECK_EQ(int(loaded->get("value")), 23);
}

TEST_CASE("[ResourceFormatNamespace] A saved text resource round-trips its qualified type") {
	ensure_resource_registrations();

	Ref<Resource> source = Ref<Resource>(Object::cast_to<Resource>(ClassDB::instantiate(NAMESPACED_RESOURCE)));
	REQUIRE(source.is_valid());
	source->set("value", 29);

	const String path = TestUtils::get_temp_path("resource_format_qualified_saved.tres");
	REQUIRE_EQ(ResourceSaver::save(source, path), OK);

	Ref<Resource> loaded = load_uncached(path);
	REQUIRE(loaded.is_valid());
	CHECK_EQ(loaded->get_class(), String(NAMESPACED_RESOURCE));
	CHECK_EQ(int(loaded->get("value")), 29);
}

TEST_CASE("[ResourceFormatNamespace] A namespaced sub-resource gets an identifier scene unique id") {
	ensure_resource_registrations();

	SUBCASE("text format") {
		Ref<Resource> parent = Ref<Resource>(Object::cast_to<Resource>(ClassDB::instantiate(NAMESPACED_RESOURCE)));
		Ref<Resource> child = Ref<Resource>(Object::cast_to<Resource>(ClassDB::instantiate(NAMESPACED_RESOURCE)));
		REQUIRE(parent.is_valid());
		REQUIRE(child.is_valid());
		parent->set("value", 41);
		child->set("value", 42);
		parent->set("child", child);

		const String path = TestUtils::get_temp_path("resource_format_qualified_sub_saved.tres");
		REQUIRE_EQ(ResourceSaver::save(parent, path), OK);
		check_derived_scene_unique_id(child, NAMESPACED_RESOURCE);

		// The written sub-resource header keeps the qualified type but uses the derived identifier id.
		Ref<FileAccess> saved = FileAccess::open(path, FileAccess::READ);
		REQUIRE(saved.is_valid());
		const String text = saved->get_as_text();
		CHECK(text.contains(vformat("[sub_resource type=\"%s\" id=\"%s\"]",
				NAMESPACED_RESOURCE, child->get_scene_unique_id())));

		Ref<Resource> loaded = load_uncached(path);
		REQUIRE(loaded.is_valid());
		CHECK_EQ(int(loaded->get("value")), 41);
		Ref<Resource> loaded_child = loaded->get("child");
		REQUIRE(loaded_child.is_valid());
		CHECK_EQ(loaded_child->get_class(), String(NAMESPACED_RESOURCE));
		CHECK_EQ(int(loaded_child->get("value")), 42);
	}

	SUBCASE("binary format") {
		Ref<Resource> parent = Ref<Resource>(Object::cast_to<Resource>(ClassDB::instantiate(NAMESPACED_RESOURCE)));
		Ref<Resource> child = Ref<Resource>(Object::cast_to<Resource>(ClassDB::instantiate(NAMESPACED_RESOURCE)));
		REQUIRE(parent.is_valid());
		REQUIRE(child.is_valid());
		parent->set("value", 51);
		child->set("value", 52);
		parent->set("child", child);

		const String path = TestUtils::get_temp_path("resource_format_qualified_sub_saved.res");
		REQUIRE_EQ(ResourceSaver::save(parent, path), OK);
		check_derived_scene_unique_id(parent, NAMESPACED_RESOURCE);
		check_derived_scene_unique_id(child, NAMESPACED_RESOURCE);

		Ref<Resource> loaded = load_uncached(path);
		REQUIRE(loaded.is_valid());
		CHECK_EQ(int(loaded->get("value")), 51);
		Ref<Resource> loaded_child = loaded->get("child");
		REQUIRE(loaded_child.is_valid());
		CHECK_EQ(loaded_child->get_class(), String(NAMESPACED_RESOURCE));
		CHECK_EQ(int(loaded_child->get("value")), 52);
	}
}

TEST_CASE("[ResourceFormatNamespace] A unique global alias loads and re-saves the canonical key") {
	ensure_resource_registrations();
	REQUIRE_EQ(ClassDB::resolve_type_name(UNIQUE_ALIAS), StringName(NAMESPACED_RESOURCE));

	const String path = write_scratch_file("resource_format_alias.tres",
			vformat("[gd_resource type=\"%s\" format=3]\n\n[resource]\nvalue = 3\n", UNIQUE_ALIAS));

	Ref<Resource> loaded = load_uncached(path);
	REQUIRE(loaded.is_valid());
	CHECK(Object::cast_to<MissingResource>(loaded.ptr()) == nullptr);
	CHECK_EQ(loaded->get_class(), String(NAMESPACED_RESOURCE));
	CHECK_EQ(int(loaded->get("value")), 3);

	// The alias is a load-time re-export only; saving normalizes to the canonical key.
	const String resaved_path = TestUtils::get_temp_path("resource_format_alias_resaved.tres");
	REQUIRE_EQ(ResourceSaver::save(loaded, resaved_path), OK);

	Ref<FileAccess> resaved = FileAccess::open(resaved_path, FileAccess::READ);
	REQUIRE(resaved.is_valid());
	const String header = resaved->get_line();
	CHECK(header.contains(vformat("type=\"%s\"", NAMESPACED_RESOURCE)));
	CHECK_FALSE(header.contains(UNIQUE_ALIAS));
}

TEST_CASE("[ResourceFormatNamespace] An ambiguous alias falls back to a MissingResource") {
	ensure_resource_registrations();
	REQUIRE_EQ(ClassDB::resolve_type_name(AMBIGUOUS_ALIAS), StringName());

	const MissingResourceRecordingScope recording_scope;

	const String path = write_scratch_file("resource_format_ambiguous_alias.tres",
			vformat("[gd_resource type=\"%s\" format=3]\n\n[resource]\n", AMBIGUOUS_ALIAS));

	Ref<Resource> loaded = load_uncached(path);
	REQUIRE(loaded.is_valid());
	MissingResource *missing = Object::cast_to<MissingResource>(loaded.ptr());
	REQUIRE_NE(missing, nullptr);
	// The unresolved on-disk string is preserved verbatim, not the resolver's empty answer.
	CHECK_EQ(missing->get_original_class(), String(AMBIGUOUS_ALIAS));
}

TEST_CASE("[ResourceFormatNamespace] The bare name of a namespaced resource falls back to a MissingResource") {
	ensure_resource_registrations();
	REQUIRE_EQ(ClassDB::resolve_type_name("_ResourceFormatNamespacedResource"), StringName());

	const MissingResourceRecordingScope recording_scope;

	const String path = write_scratch_file("resource_format_bare_name.tres",
			"[gd_resource type=\"_ResourceFormatNamespacedResource\" format=3]\n\n[resource]\n");

	Ref<Resource> loaded = load_uncached(path);
	REQUIRE(loaded.is_valid());
	MissingResource *missing = Object::cast_to<MissingResource>(loaded.ptr());
	REQUIRE_NE(missing, nullptr);
	CHECK_EQ(missing->get_original_class(), String("_ResourceFormatNamespacedResource"));
}

} // namespace TestResourceFormatNamespace
