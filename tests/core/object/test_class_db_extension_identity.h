/**************************************************************************/
/*  test_class_db_extension_identity.h                                    */
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

#include "core/object/class_db.h"
#include "core/object/object.h"

#include "tests/test_macros.h"

namespace TestClassDBExtensionIdentity {

// Owns a fabricated extension class registration for the duration of the scope, which is how these
// cases observe the registry identity `register_extension_class()` gives an extension class without
// needing a real extension library.
class ExtensionClassScope {
	ObjectFoundryExtension extension = {};
	bool registered = false;

public:
	ExtensionClassScope(const StringName &p_class_name, const StringName &p_parent_class_name) {
		extension.class_name = p_class_name;
		extension.parent_class_name = p_parent_class_name;
		extension.create_gdtype();

		ClassDB::register_extension_class(&extension);
		registered = ClassDB::class_exists(p_class_name);
	}

	~ExtensionClassScope() {
		unregister();
	}

	void unregister() {
		if (!registered) {
			return;
		}
		registered = false;
		ClassDB::unregister_extension_class(extension.class_name);
	}
};

TEST_CASE("[ClassDBExtensionIdentity] An extension class enters the registry with a canonical name") {
	const StringName class_name = "_ExtensionIdentityTest";

	REQUIRE_FALSE(ClassDB::class_exists(class_name));

	ExtensionClassScope scope(class_name, "RefCounted");

	CHECK(ClassDB::class_exists(class_name));
	CHECK(ClassDB::resolve_type_name(class_name) == class_name);
	CHECK(ClassDB::class_get_qualified_name(class_name) == class_name);
	CHECK(ClassDB::class_get_namespace(class_name) == StringName());
	// Reflexive and inherited identity both answer on the canonical key.
	CHECK(ClassDB::is_parent_class(class_name, class_name));
	CHECK(ClassDB::is_parent_class(class_name, "RefCounted"));
	CHECK(ClassDB::is_parent_class(class_name, "Object"));
	CHECK_FALSE(ClassDB::is_parent_class(class_name, "Node"));
	CHECK(ClassDB::get_parent_class(class_name) == StringName("RefCounted"));

	StringName simple_name;
	CHECK(ClassDB::class_get_by_qualified_name(class_name, simple_name));
	CHECK(simple_name == class_name);

	scope.unregister();

	CHECK_FALSE(ClassDB::class_exists(class_name));
	CHECK(ClassDB::resolve_type_name(class_name) == StringName());
	CHECK_FALSE(ClassDB::is_parent_class(class_name, class_name));
	CHECK_FALSE(ClassDB::is_parent_class(class_name, "RefCounted"));
}

TEST_CASE("[ClassDBExtensionIdentity] Unregistering an extension class drops the names it owns") {
	const StringName class_name = "_ExtensionIdentityAliasedTest";
	const StringName alias = "_ExtensionIdentityAlias";

	REQUIRE_FALSE(ClassDB::class_exists(class_name));

	{
		ExtensionClassScope scope(class_name, "RefCounted");
		ClassDB::class_register_global_alias(class_name, alias);
		CHECK(ClassDB::resolve_type_name(alias) == class_name);
	}

	// A reloaded extension must not inherit a re-export pointing at the class it replaced.
	CHECK_FALSE(ClassDB::class_exists(alias));
	CHECK(ClassDB::resolve_type_name(alias) == StringName());
	CHECK(ClassDB::class_get_qualified_name(class_name) == StringName());
}

} // namespace TestClassDBExtensionIdentity
