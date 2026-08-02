/**************************************************************************/
/*  test_foundry_extension_interface.h                                    */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             FOUNDRY ENGINE                             */
/*          A fork of the Godot Engine (https://godotengine.org)          */
/*                       https://www.cafecito.games                       */
/**************************************************************************/
/* Copyright (c) 2026-present Cafecito Games LLC.                         */
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

#include "core/extension/foundry_extension.h"
#include "core/variant/variant.h"
#include "core/variant/variant_internal.h"

#include "tests/test_macros.h"

#include <climits>

// Declared with plain C++ linkage in `foundry_extension_interface.cpp`, mirroring the extern
// declaration `foundry_extension.cpp` uses to hand this address to loaded extensions.
extern FoundryExtensionInterfaceFunctionPtr foundry_extension_get_proc_address(const char *p_name);

namespace TestFoundryExtensionInterface {

static void *get_interface_function(const char *p_name) {
	FoundryExtensionInterfaceFunctionPtr fn = foundry_extension_get_proc_address(p_name);
	REQUIRE_MESSAGE(fn != nullptr, vformat("Missing FoundryExtension interface function '%s'.", p_name).utf8().get_data());
	return (void *)fn;
}

// No ordinary C++ constructor yields `Variant::UINT` yet (unsigned integer literals still select
// the signed carrier), so tests build one explicitly through `VariantInternal`.
static Variant make_uint_variant(uint64_t p_value) {
	Variant v;
	VariantInternal::set_type(v, Variant::UINT);
	*VariantInternal::get_uint(&v) = p_value;
	return v;
}

TEST_CASE("[FoundryExtensionInterface][UInt] Public enum identity is stable") {
	CHECK(FOUNDRY_EXTENSION_VARIANT_TYPE_UINT == 39);
	CHECK(FOUNDRY_EXTENSION_VARIANT_TYPE_VARIANT_MAX == 40);
	// Every older public value stays at its previously published numeric value.
	CHECK(FOUNDRY_EXTENSION_VARIANT_TYPE_NIL == 0);
	CHECK(FOUNDRY_EXTENSION_VARIANT_TYPE_INT == 2);
	CHECK(FOUNDRY_EXTENSION_VARIANT_TYPE_PACKED_VECTOR4_ARRAY == 38);
}

TEST_CASE("[FoundryExtensionInterface][UInt] variant_get_type reports UINT for the unsigned carrier") {
	using VariantGetTypeFunc = FoundryExtensionVariantType (*)(FoundryExtensionConstVariantPtr);
	VariantGetTypeFunc variant_get_type = (VariantGetTypeFunc)get_interface_function("variant_get_type");

	Variant source = make_uint_variant(UINT64_MAX);
	FoundryExtensionVariantType reported = variant_get_type(&source);
	CHECK(reported == FOUNDRY_EXTENSION_VARIANT_TYPE_UINT);
}

TEST_CASE("[FoundryExtensionInterface][UInt] Construction and extraction round trip upper-half values") {
	using GetVariantFromTypeConstructorFunc = FoundryExtensionVariantFromTypeConstructorFunc (*)(FoundryExtensionVariantType);
	using GetVariantToTypeConstructorFunc = FoundryExtensionTypeFromVariantConstructorFunc (*)(FoundryExtensionVariantType);
	using VariantDestroyFunc = void (*)(FoundryExtensionVariantPtr);

	GetVariantFromTypeConstructorFunc get_variant_from_type_constructor =
			(GetVariantFromTypeConstructorFunc)get_interface_function("get_variant_from_type_constructor");
	GetVariantToTypeConstructorFunc get_variant_to_type_constructor =
			(GetVariantToTypeConstructorFunc)get_interface_function("get_variant_to_type_constructor");
	VariantDestroyFunc variant_destroy = (VariantDestroyFunc)get_interface_function("variant_destroy");

	FoundryExtensionVariantFromTypeConstructorFunc from_type = get_variant_from_type_constructor(FOUNDRY_EXTENSION_VARIANT_TYPE_UINT);
	REQUIRE(from_type != nullptr);
	FoundryExtensionTypeFromVariantConstructorFunc to_type = get_variant_to_type_constructor(FOUNDRY_EXTENSION_VARIANT_TYPE_UINT);
	REQUIRE(to_type != nullptr);

	const uint64_t upper_half_values[] = { 0, uint64_t(INT64_MAX), uint64_t(INT64_MAX) + 1, UINT64_MAX };

	for (uint64_t value : upper_half_values) {
		alignas(Variant) uint8_t variant_storage[sizeof(Variant)];
		from_type(variant_storage, &value);
		Variant *constructed = reinterpret_cast<Variant *>(variant_storage);
		CHECK(constructed->get_type() == Variant::UINT);
		CHECK(constructed->operator uint64_t() == value);

		uint64_t extracted = 0;
		to_type(&extracted, variant_storage);
		CHECK(extracted == value);

		variant_destroy(variant_storage);
	}
}

TEST_CASE("[FoundryExtensionInterface][UInt] Pointer-call internal getter exposes the raw carrier") {
	using GetPtrInternalGetterFunc = FoundryExtensionVariantGetInternalPtrFunc (*)(FoundryExtensionVariantType);
	GetPtrInternalGetterFunc variant_get_ptr_internal_getter =
			(GetPtrInternalGetterFunc)get_interface_function("variant_get_ptr_internal_getter");

	FoundryExtensionVariantGetInternalPtrFunc getter = variant_get_ptr_internal_getter(FOUNDRY_EXTENSION_VARIANT_TYPE_UINT);
	REQUIRE(getter != nullptr);

	Variant source = make_uint_variant(UINT64_MAX);
	void *raw = getter(&source);
	REQUIRE(raw != nullptr);
	CHECK(*reinterpret_cast<uint64_t *>(raw) == UINT64_MAX);
}

} // namespace TestFoundryExtensionInterface
