/**************************************************************************/
/*  test_variant_char_carrier.h                                           */
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

#include "core/input/input_event.h"
#include "core/variant/variant.h"

#include "tests/test_macros.h"

namespace TestVariantCharCarrier {

TEST_CASE("[Variant] char32_t and char16_t construct the signed carrier") {
	// `GetTypeInfo<char32_t>`/`GetTypeInfo<char16_t>` declare `Variant::INT` (see
	// `METADATA_INT_IS_CHAR32`/`METADATA_INT_IS_CHAR16`), so the runtime constructor must agree
	// instead of falling back to a standard conversion into the unsigned overloads.
	CHECK(Variant(char32_t(0)).get_type() == Variant::INT);
	CHECK(Variant(char32_t(0x1F600)).get_type() == Variant::INT);
	CHECK(Variant(char16_t(0)).get_type() == Variant::INT);
	CHECK(Variant(char16_t(0xFFFF)).get_type() == Variant::INT);

	const Variant emoji = char32_t(0x1F600);
	CHECK(emoji.operator int64_t() == 0x1F600);

	const Variant surrogate = char16_t(0xFFFF);
	CHECK(surrogate.operator int64_t() == 0xFFFF);
	CHECK(emoji.stringify() == "128512");
}

TEST_CASE("[Variant] A char32_t getter reached through Object::call carries Variant::INT") {
	// `InputEventKey::get_unicode()` returns `char32_t` and is reached generically the same way
	// the documentation tool and script property access reach it: through `MethodBind::call()`,
	// which builds its return value with `VariantInternal::make()` doing plain `Variant(v)`
	// overload resolution. Before the dedicated `char32_t` constructor, that selected
	// `Variant(uint32_t)` and produced `Variant::UINT`, contradicting the declared `Variant::INT`
	// carrier the "unicode" property registers with `ADD_PROPERTY`.
	Ref<InputEventKey> event;
	event.instantiate();
	event->set_unicode(U'A');

	const Variant unicode = event->call("get_unicode");
	CHECK(unicode.get_type() == Variant::INT);
	CHECK(unicode.operator int64_t() == int64_t(U'A'));

	const Variant property_value = event->get("unicode");
	CHECK(property_value.get_type() == Variant::INT);
	CHECK(property_value.operator int64_t() == int64_t(U'A'));
}

} // namespace TestVariantCharCarrier
