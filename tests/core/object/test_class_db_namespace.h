/**************************************************************************/
/*  test_class_db_namespace.h                                             */
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

// Declared in global namespace because of FOUNDRY_CLASS macro warning (Windows):
// "Unqualified friend declaration referring to type outside of the nearest enclosing namespace
// is a Microsoft extension; add a nested name specifier".
class _NamespaceTestHttp : public Object {
	FOUNDRY_CLASS(_NamespaceTestHttp, Object);

	int property_value = 0;

protected:
	static void _bind_methods() {
		ClassDB::bind_method(D_METHOD("set_property", "property"), &_NamespaceTestHttp::set_property);
		ClassDB::bind_method(D_METHOD("get_property"), &_NamespaceTestHttp::get_property);
		ADD_PROPERTY(PropertyInfo(Variant::INT, "property"), "set_property", "get_property");
	}

public:
	void set_property(int p_value) { property_value = p_value; }
	int get_property() const { return property_value; }
};

class _NamespaceTestFtp : public Object {
	FOUNDRY_CLASS(_NamespaceTestFtp, Object);
};

class _NamespaceTestBase : public Object {
	FOUNDRY_CLASS(_NamespaceTestBase, Object);
};

class _NamespaceTestSubclass : public _NamespaceTestBase {
	FOUNDRY_CLASS(_NamespaceTestSubclass, _NamespaceTestBase);
};

class _NamespaceTestParent : public Object {
	FOUNDRY_CLASS(_NamespaceTestParent, Object);
};

class _NamespaceTestChild : public _NamespaceTestParent {
	FOUNDRY_CLASS(_NamespaceTestChild, _NamespaceTestParent);
};

namespace TestClassDBNamespace {

TEST_CASE("[ClassDBNamespace] Classes without a namespace keep their bare identity") {
	CHECK(ClassDB::class_exists("Object"));
	CHECK(ClassDB::class_get_qualified_name("Object") == StringName("Object"));
	CHECK(ClassDB::class_get_namespace("Object") == StringName());
	CHECK(ClassDB::resolve_type_name("Object") == StringName("Object"));
	CHECK(ClassDB::class_get_in_namespace(StringName(), "Object") == StringName("Object"));

	StringName simple_name;
	CHECK(ClassDB::class_get_by_qualified_name("Object", simple_name));
	CHECK(simple_name == StringName("Object"));

	// Unknown names resolve to nothing rather than erroring.
	CHECK(ClassDB::class_get_qualified_name("_NamespaceTestNotAClass") == StringName());
	CHECK(ClassDB::class_get_namespace("_NamespaceTestNotAClass") == StringName());
	CHECK(ClassDB::resolve_type_name("_NamespaceTestNotAClass") == StringName());
	CHECK_FALSE(ClassDB::class_get_by_qualified_name("_NamespaceTestNotAClass", simple_name));
}

TEST_CASE("[ClassDBNamespace] Namespacing rekeys the registry and stamps runtime identity") {
	const StringName http_qualified = "foundry.test.http._NamespaceTestHttp";
	const StringName ftp_qualified = "foundry.test.ftp._NamespaceTestFtp";

	// Namespacing is a one-shot rekey, so the shared setup must not repeat per subcase.
	static bool namespaces_registered = false;
	if (!namespaces_registered) {
		namespaces_registered = true;
		FOUNDRY_REGISTER_CLASS(_NamespaceTestHttp);
		FOUNDRY_REGISTER_CLASS(_NamespaceTestFtp);
		CHECK(ClassDB::class_exists("_NamespaceTestHttp"));
		FOUNDRY_REGISTER_NAMESPACE(_NamespaceTestHttp, "foundry.test.http");
		FOUNDRY_REGISTER_NAMESPACE(_NamespaceTestFtp, "foundry.test.ftp");
	}

	SUBCASE("The bare name no longer resolves but the qualified name does") {
		CHECK_FALSE(ClassDB::class_exists("_NamespaceTestHttp"));
		CHECK(ClassDB::class_exists(http_qualified));
		CHECK(ClassDB::class_get_qualified_name(http_qualified) == http_qualified);
		CHECK(ClassDB::class_get_namespace(http_qualified) == StringName("foundry.test.http"));
		CHECK(ClassDB::resolve_type_name(http_qualified) == http_qualified);
		CHECK(ClassDB::resolve_type_name("_NamespaceTestHttp") == StringName());

		StringName simple_name;
		CHECK(ClassDB::class_get_by_qualified_name(http_qualified, simple_name));
		CHECK(simple_name == StringName("_NamespaceTestHttp"));
		CHECK_FALSE(ClassDB::class_get_by_qualified_name("_NamespaceTestHttp", simple_name));

		CHECK(ClassDB::class_get_in_namespace("foundry.test.http", "_NamespaceTestHttp") == http_qualified);
		CHECK(ClassDB::class_get_in_namespace("foundry.test.ftp", "_NamespaceTestHttp") == StringName());
	}

	SUBCASE("The C++ simple name still maps to the qualified name for tooling") {
		CHECK(ClassDB::class_get_qualified_name("_NamespaceTestHttp") == http_qualified);
		CHECK(ClassDB::class_get_namespace("_NamespaceTestHttp") == StringName("foundry.test.http"));
	}

	SUBCASE("The static class name follows the stamp") {
		// `FOUNDRY_CLASS` resolves property lists through `get_class_static()`, so it has to be the
		// registry key rather than the stringified class token.
		CHECK(_NamespaceTestHttp::get_class_static() == http_qualified);
	}

	SUBCASE("A namespaced class instantiates and carries the qualified runtime identity") {
		Object *instance = ClassDB::instantiate(http_qualified);
		REQUIRE(instance != nullptr);
		CHECK(instance->get_class() == String(http_qualified));
		CHECK(instance->is_class("Object"));
		CHECK_FALSE(instance->is_class("_NamespaceTestHttp"));

		bool valid = false;
		instance->set("property", 42, &valid);
		CHECK(valid);
		CHECK(int(instance->get("property")) == 42);

		List<PropertyInfo> property_list;
		instance->get_property_list(&property_list);
		bool found_property = false;
		for (const PropertyInfo &property_info : property_list) {
			if (property_info.name == StringName("property") && property_info.type == Variant::INT) {
				found_property = true;
				break;
			}
		}
		CHECK(found_property);

		memdelete(instance);
	}

	SUBCASE("A unique global alias re-exports the bare name, two owners are ambiguous") {
		ClassDB::class_register_global_alias(http_qualified, "_NamespaceTestServer");
		CHECK(ClassDB::class_exists("_NamespaceTestServer"));
		CHECK(ClassDB::resolve_type_name("_NamespaceTestServer") == http_qualified);

		Object *instance = ClassDB::instantiate("_NamespaceTestServer");
		REQUIRE(instance != nullptr);
		CHECK(instance->get_class() == String(http_qualified));
		memdelete(instance);

		ClassDB::class_register_global_alias(ftp_qualified, "_NamespaceTestServer");
		CHECK_FALSE(ClassDB::class_exists("_NamespaceTestServer"));
		CHECK(ClassDB::resolve_type_name("_NamespaceTestServer") == StringName());

		// Both qualified names keep resolving regardless of the ambiguous alias.
		CHECK(ClassDB::resolve_type_name(http_qualified) == http_qualified);
		CHECK(ClassDB::resolve_type_name(ftp_qualified) == ftp_qualified);
	}

	SUBCASE("Namespacing an already namespaced class fails") {
		ERR_PRINT_OFF;
		ClassDB::register_namespace("_NamespaceTestHttp", "foundry.test.other");
		ERR_PRINT_ON;
		CHECK(ClassDB::class_exists(http_qualified));
		CHECK_FALSE(ClassDB::class_exists("foundry.test.other._NamespaceTestHttp"));
	}
}

TEST_CASE("[ClassDBNamespace] A subclass registered after its parent is namespaced inherits through the qualified name") {
	const StringName parent_qualified = "foundry.test.parent._NamespaceTestParent";

	static bool parent_registered = false;
	if (!parent_registered) {
		parent_registered = true;
		FOUNDRY_REGISTER_CLASS(_NamespaceTestParent);
		FOUNDRY_REGISTER_NAMESPACE(_NamespaceTestParent, "foundry.test.parent");
		FOUNDRY_REGISTER_CLASS(_NamespaceTestChild);
	}

	// The child itself is not namespaced, so it keeps a bare canonical key.
	CHECK(ClassDB::class_exists("_NamespaceTestChild"));
	CHECK(ClassDB::class_get_qualified_name("_NamespaceTestChild") == StringName("_NamespaceTestChild"));
	CHECK(ClassDB::get_parent_class("_NamespaceTestChild") == parent_qualified);
	CHECK(ClassDB::is_parent_class("_NamespaceTestChild", parent_qualified));
	// The namespaced ancestor must not be reachable by its bare name.
	CHECK_FALSE(ClassDB::is_parent_class("_NamespaceTestChild", "_NamespaceTestParent"));
	CHECK(ClassDB::is_parent_class("_NamespaceTestChild", "Object"));

	Object *instance = ClassDB::instantiate("_NamespaceTestChild");
	REQUIRE(instance != nullptr);
	CHECK(instance->get_class() == String("_NamespaceTestChild"));
	CHECK(instance->is_class(String(parent_qualified)));
	CHECK_FALSE(instance->is_class("_NamespaceTestParent"));
	CHECK(instance->is_class("Object"));
	memdelete(instance);
}

TEST_CASE("[ClassDBNamespace] Invalid namespace registrations are rejected") {
	FOUNDRY_REGISTER_CLASS(_NamespaceTestBase);
	FOUNDRY_REGISTER_CLASS(_NamespaceTestSubclass);

	SUBCASE("An unknown class cannot be namespaced") {
		ERR_PRINT_OFF;
		ClassDB::register_namespace("_NamespaceTestNotAClass", "foundry.test.unknown");
		ERR_PRINT_ON;
		CHECK_FALSE(ClassDB::class_exists("foundry.test.unknown._NamespaceTestNotAClass"));
	}

	SUBCASE("An empty namespace is rejected") {
		ERR_PRINT_OFF;
		ClassDB::register_namespace("_NamespaceTestBase", StringName());
		ERR_PRINT_ON;
		CHECK(ClassDB::class_exists("_NamespaceTestBase"));
		CHECK(ClassDB::class_get_namespace("_NamespaceTestBase") == StringName());
	}

	SUBCASE("A class with an already registered subclass cannot be namespaced") {
		ERR_PRINT_OFF;
		ClassDB::register_namespace("_NamespaceTestBase", "foundry.test.base");
		ERR_PRINT_ON;
		CHECK(ClassDB::class_exists("_NamespaceTestBase"));
		CHECK_FALSE(ClassDB::class_exists("foundry.test.base._NamespaceTestBase"));
	}

	SUBCASE("Aliasing an unknown class is rejected") {
		ERR_PRINT_OFF;
		ClassDB::class_register_global_alias("foundry.test.unknown._NamespaceTestNotAClass", "_NamespaceTestUnknownAlias");
		ERR_PRINT_ON;
		CHECK_FALSE(ClassDB::class_exists("_NamespaceTestUnknownAlias"));
	}

	SUBCASE("An alias shadowed by a canonical class key is rejected") {
		// It could never resolve, since a canonical class always wins over a re-export.
		ERR_PRINT_OFF;
		ClassDB::class_register_global_alias("Object", "_NamespaceTestBase");
		ERR_PRINT_ON;
		CHECK(ClassDB::resolve_type_name("_NamespaceTestBase") == StringName("_NamespaceTestBase"));
	}
}

TEST_CASE("[ClassDBNamespace] The HTTPServer pilot is reachable only through its namespace") {
	const StringName qualified_name = "foundry.http.server.HTTPServer";

	CHECK(ClassDB::class_exists(qualified_name));
	CHECK_FALSE(ClassDB::class_exists("HTTPServer"));
	CHECK(ClassDB::class_get_namespace(qualified_name) == StringName("foundry.http.server"));
	CHECK(ClassDB::class_get_namespace("HTTPServer") == StringName("foundry.http.server"));
	CHECK(ClassDB::class_get_qualified_name("HTTPServer") == qualified_name);
	CHECK(ClassDB::class_get_in_namespace("foundry.http.server", "HTTPServer") == qualified_name);
	CHECK(ClassDB::resolve_type_name(qualified_name) == qualified_name);
	// No global alias is registered for the pilot, so the bare name stays unresolvable.
	CHECK(ClassDB::resolve_type_name("HTTPServer") == StringName());

	StringName simple_name;
	CHECK(ClassDB::class_get_by_qualified_name(qualified_name, simple_name));
	CHECK(simple_name == StringName("HTTPServer"));

	// Inheritance queries answer on canonical keys: a class is its own ancestor under its qualified
	// name, inherited flat ancestors keep matching, and the bare name is not an identity.
	CHECK(ClassDB::is_parent_class(qualified_name, qualified_name));
	CHECK(ClassDB::is_parent_class(qualified_name, "Node"));
	CHECK(ClassDB::is_parent_class(qualified_name, "Object"));
	CHECK_FALSE(ClassDB::is_parent_class(qualified_name, "HTTPServer"));
	CHECK_FALSE(ClassDB::is_parent_class(qualified_name, "Node2D"));
	// Flat classes are unaffected by the qualified-name comparison.
	CHECK(ClassDB::is_parent_class("Node2D", "Node2D"));
	CHECK(ClassDB::is_parent_class("Node2D", "Node"));
	CHECK_FALSE(ClassDB::is_parent_class("Node", "Node2D"));
	CHECK_FALSE(ClassDB::is_parent_class("Node", qualified_name));
}

TEST_CASE("[ClassDBNamespace] The HTTPServer pilot instantiates with a qualified runtime identity") {
	const StringName qualified_name = "foundry.http.server.HTTPServer";

	Object *instance = ClassDB::instantiate(qualified_name);
	REQUIRE(instance != nullptr);

	CHECK(instance->get_class() == String(qualified_name));
	CHECK(instance->is_class("Node"));
	CHECK(instance->is_class("Object"));
	CHECK_FALSE(instance->is_class("HTTPServer"));

	CHECK(int(instance->get("port")) == 8080);

	bool valid = false;
	instance->set("port", 9000, &valid);
	CHECK(valid);
	CHECK(int(instance->get("port")) == 9000);
	CHECK(int(instance->call("get_port")) == 9000);

	// Out-of-range ports are rejected and leave the previous value untouched, through both the
	// bound method and the property path.
	ERR_PRINT_OFF;
	instance->call("set_port", 70000);
	instance->set("port", -1, &valid);
	ERR_PRINT_ON;
	CHECK(int(instance->call("get_port")) == 9000);
	CHECK(int(instance->get("port")) == 9000);

	CHECK(bool(instance->call("start")));
	instance->call("stop");

	memdelete(instance);
}

} // namespace TestClassDBNamespace
