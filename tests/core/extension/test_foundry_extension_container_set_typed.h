/**************************************************************************/
/*  test_foundry_extension_container_set_typed.h                          */
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
#include "core/variant/array.h"
#include "core/variant/container_type_validate.h"
#include "core/variant/dictionary.h"
#include "core/variant/numeric_type.h"

#include "tests/test_macros.h"

// Declared with plain C++ linkage in `foundry_extension_interface.cpp`, mirroring the extern
// declaration `foundry_extension.cpp` uses to hand this address to loaded extensions.
extern FoundryExtensionInterfaceFunctionPtr foundry_extension_get_proc_address(const char *p_name);

namespace TestFoundryExtensionContainerSetTyped {

static void *get_interface_function(const char *p_name) {
	FoundryExtensionInterfaceFunctionPtr fn = foundry_extension_get_proc_address(p_name);
	REQUIRE_MESSAGE(fn != nullptr, vformat("Missing FoundryExtension interface function '%s'.", p_name).utf8().get_data());
	return (void *)fn;
}

using ArraySetTypedByDescriptorFunc = FoundryExtensionBool (*)(FoundryExtensionTypePtr, FoundryExtensionConstVariantPtr);
using DictionarySetTypedByDescriptorFunc = FoundryExtensionBool (*)(FoundryExtensionTypePtr, FoundryExtensionConstVariantPtr, FoundryExtensionConstVariantPtr);

static ContainerType make_numeric_type(Variant::Type p_carrier, NumericType p_numeric_type) {
	ContainerType type;
	type.builtin_type = p_carrier;
	type.numeric_type = p_numeric_type;
	return type;
}

TEST_CASE("[FoundryExtensionInterface][ContainerSetTyped] Array descriptor rejected by a numeric-carrier mismatch reports failure") {
	ArraySetTypedByDescriptorFunc array_set_typed_by_descriptor =
			(ArraySetTypedByDescriptorFunc)get_interface_function("array_set_typed_by_descriptor");

	Array array;
	// STRING carrying an INT32 width constraint is a carrier/width mismatch. The descriptor codec
	// itself already refuses to decode this shape, so this exercises the same "invalid descriptor"
	// path the interface function already reports correctly; it is here to pin that behavior.
	Variant descriptor = ContainerTypeDescriptor::to_variant(make_numeric_type(Variant::STRING, NumericType::INT32));

	ERR_PRINT_OFF;
	FoundryExtensionBool ok = array_set_typed_by_descriptor((FoundryExtensionTypePtr)&array, (FoundryExtensionConstVariantPtr)&descriptor);
	ERR_PRINT_ON;

	CHECK_FALSE(ok);
	CHECK_FALSE(array.is_typed());
}

TEST_CASE("[FoundryExtensionInterface][ContainerSetTyped] Array descriptor rejected because the array is already typed reports failure") {
	ArraySetTypedByDescriptorFunc array_set_typed_by_descriptor =
			(ArraySetTypedByDescriptorFunc)get_interface_function("array_set_typed_by_descriptor");

	Array array;
	Variant descriptor = ContainerTypeDescriptor::to_variant(make_numeric_type(Variant::INT, NumericType::INT32));
	FoundryExtensionBool first_ok = array_set_typed_by_descriptor((FoundryExtensionTypePtr)&array, (FoundryExtensionConstVariantPtr)&descriptor);
	REQUIRE(first_ok);

	// The descriptor is valid on its own, so `ContainerTypeDescriptor::from_variant()` has nothing to
	// object to. The rejection here comes entirely from `Array::set_typed()`'s pre-existing "type can
	// only be set once" guard, which predates the numeric-carrier guard added alongside #1562.
	Variant other_descriptor = ContainerTypeDescriptor::to_variant(make_numeric_type(Variant::FLOAT, NumericType::NONE));
	ERR_PRINT_OFF;
	FoundryExtensionBool second_ok = array_set_typed_by_descriptor((FoundryExtensionTypePtr)&array, (FoundryExtensionConstVariantPtr)&other_descriptor);
	ERR_PRINT_ON;

	CHECK_FALSE(second_ok);
	// The original type must survive the rejected call untouched.
	CHECK_EQ(array.get_element_type(), make_numeric_type(Variant::INT, NumericType::INT32));
}

TEST_CASE("[FoundryExtensionInterface][ContainerSetTyped] Array descriptor rejected because the array is non-empty reports failure") {
	ArraySetTypedByDescriptorFunc array_set_typed_by_descriptor =
			(ArraySetTypedByDescriptorFunc)get_interface_function("array_set_typed_by_descriptor");

	Array array;
	array.push_back(Variant(int64_t(1)));

	// Another pre-existing guard unrelated to the numeric-carrier work: `Array::set_typed()` refuses
	// to type a non-empty array.
	Variant descriptor = ContainerTypeDescriptor::to_variant(make_numeric_type(Variant::INT, NumericType::NONE));
	ERR_PRINT_OFF;
	FoundryExtensionBool ok = array_set_typed_by_descriptor((FoundryExtensionTypePtr)&array, (FoundryExtensionConstVariantPtr)&descriptor);
	ERR_PRINT_ON;

	CHECK_FALSE(ok);
	CHECK_FALSE(array.is_typed());
}

TEST_CASE("[FoundryExtensionInterface][ContainerSetTyped] Array descriptor accepted by set_typed reports success") {
	ArraySetTypedByDescriptorFunc array_set_typed_by_descriptor =
			(ArraySetTypedByDescriptorFunc)get_interface_function("array_set_typed_by_descriptor");

	Array array;
	Variant descriptor = ContainerTypeDescriptor::to_variant(make_numeric_type(Variant::INT, NumericType::INT32));

	FoundryExtensionBool ok = array_set_typed_by_descriptor((FoundryExtensionTypePtr)&array, (FoundryExtensionConstVariantPtr)&descriptor);

	CHECK(ok);
	CHECK(array.is_typed());
}

TEST_CASE("[FoundryExtensionInterface][ContainerSetTyped] Dictionary descriptor rejected by a numeric-carrier mismatch reports failure") {
	DictionarySetTypedByDescriptorFunc dictionary_set_typed_by_descriptor =
			(DictionarySetTypedByDescriptorFunc)get_interface_function("dictionary_set_typed_by_descriptor");

	Dictionary dictionary;
	// Same shape as the Array case above: the descriptor codec already refuses to decode this, so
	// this pins the existing "invalid descriptor" behavior rather than the swallowed-guard bug.
	Variant key_descriptor = ContainerTypeDescriptor::to_variant(make_numeric_type(Variant::STRING, NumericType::UINT32));
	Variant value_descriptor = ContainerTypeDescriptor::to_variant(ContainerType());

	ERR_PRINT_OFF;
	FoundryExtensionBool ok = dictionary_set_typed_by_descriptor((FoundryExtensionTypePtr)&dictionary,
			(FoundryExtensionConstVariantPtr)&key_descriptor, (FoundryExtensionConstVariantPtr)&value_descriptor);
	ERR_PRINT_ON;

	CHECK_FALSE(ok);
	CHECK_FALSE(dictionary.is_typed());
}

TEST_CASE("[FoundryExtensionInterface][ContainerSetTyped] Dictionary descriptor rejected because the dictionary is already typed reports failure") {
	DictionarySetTypedByDescriptorFunc dictionary_set_typed_by_descriptor =
			(DictionarySetTypedByDescriptorFunc)get_interface_function("dictionary_set_typed_by_descriptor");

	Dictionary dictionary;
	Variant key_descriptor = ContainerTypeDescriptor::to_variant(make_numeric_type(Variant::UINT, NumericType::UINT32));
	Variant value_descriptor = ContainerTypeDescriptor::to_variant(make_numeric_type(Variant::INT, NumericType::INT32));
	FoundryExtensionBool first_ok = dictionary_set_typed_by_descriptor((FoundryExtensionTypePtr)&dictionary,
			(FoundryExtensionConstVariantPtr)&key_descriptor, (FoundryExtensionConstVariantPtr)&value_descriptor);
	REQUIRE(first_ok);

	// Both descriptors decode cleanly on their own; the rejection comes entirely from
	// `Dictionary::set_typed()`'s pre-existing "type can only be set once" guard.
	Variant other_key_descriptor = ContainerTypeDescriptor::to_variant(make_numeric_type(Variant::STRING, NumericType::NONE));
	Variant other_value_descriptor = ContainerTypeDescriptor::to_variant(make_numeric_type(Variant::STRING, NumericType::NONE));
	ERR_PRINT_OFF;
	FoundryExtensionBool second_ok = dictionary_set_typed_by_descriptor((FoundryExtensionTypePtr)&dictionary,
			(FoundryExtensionConstVariantPtr)&other_key_descriptor, (FoundryExtensionConstVariantPtr)&other_value_descriptor);
	ERR_PRINT_ON;

	CHECK_FALSE(second_ok);
	CHECK_EQ(dictionary.get_key_type(), make_numeric_type(Variant::UINT, NumericType::UINT32));
	CHECK_EQ(dictionary.get_value_type(), make_numeric_type(Variant::INT, NumericType::INT32));
}

TEST_CASE("[FoundryExtensionInterface][ContainerSetTyped] Dictionary descriptor accepted by set_typed reports success") {
	DictionarySetTypedByDescriptorFunc dictionary_set_typed_by_descriptor =
			(DictionarySetTypedByDescriptorFunc)get_interface_function("dictionary_set_typed_by_descriptor");

	Dictionary dictionary;
	Variant key_descriptor = ContainerTypeDescriptor::to_variant(make_numeric_type(Variant::UINT, NumericType::UINT32));
	Variant value_descriptor = ContainerTypeDescriptor::to_variant(make_numeric_type(Variant::INT, NumericType::INT32));

	FoundryExtensionBool ok = dictionary_set_typed_by_descriptor((FoundryExtensionTypePtr)&dictionary,
			(FoundryExtensionConstVariantPtr)&key_descriptor, (FoundryExtensionConstVariantPtr)&value_descriptor);

	CHECK(ok);
	CHECK(dictionary.is_typed());
}

} // namespace TestFoundryExtensionContainerSetTyped
