/**************************************************************************/
/*  test_editor_exact_integer_property.h                                  */
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

#include "editor/inspector/editor_properties.h"

#include "core/object/class_db.h"
#include "core/object/ref_counted.h"
#include "editor/gui/editor_spin_slider.h"
#include "scene/gui/line_edit.h"

#include "tests/test_macros.h"

// A minimal native class exposing both a signed `int` and an unsigned `uint` property, registered
// with ClassDB so `EditorInspectorDefaultPlugin` and `Object::set/get` exercise their real production
// path instead of a test-only shortcut.
class ExactIntegerPropertyFixtureObject : public RefCounted {
	FOUNDRY_CLASS(ExactIntegerPropertyFixtureObject, RefCounted);

	int64_t signed_value = 0;
	uint64_t unsigned_value = 0;

protected:
	static void _bind_methods() {
		ClassDB::bind_method(D_METHOD("set_signed_value", "value"), &ExactIntegerPropertyFixtureObject::set_signed_value);
		ClassDB::bind_method(D_METHOD("get_signed_value"), &ExactIntegerPropertyFixtureObject::get_signed_value);
		ADD_PROPERTY(PropertyInfo(Variant::INT, "signed_value"), "set_signed_value", "get_signed_value");

		ClassDB::bind_method(D_METHOD("set_unsigned_value", "value"), &ExactIntegerPropertyFixtureObject::set_unsigned_value);
		ClassDB::bind_method(D_METHOD("get_unsigned_value"), &ExactIntegerPropertyFixtureObject::get_unsigned_value);
		ADD_PROPERTY(PropertyInfo(Variant::UINT, "unsigned_value"), "set_unsigned_value", "get_unsigned_value");
	}

public:
	void set_signed_value(int64_t p_value) { signed_value = p_value; }
	int64_t get_signed_value() const { return signed_value; }
	void set_unsigned_value(uint64_t p_value) { unsigned_value = p_value; }
	uint64_t get_unsigned_value() const { return unsigned_value; }
};

// Friend accessor granting the test suite access to `EditorPropertyInteger` internals without
// widening its public API. Declared as a friend in editor/inspector/editor_properties.h.
class EditorPropertyIntegerTestAccess {
public:
	static LineEdit *get_value_edit(EditorPropertyInteger *p_editor) { return p_editor->value_edit; }
	static EditorSpinSlider *get_spin(EditorPropertyInteger *p_editor) { return p_editor->spin; }
	static bool get_is_unsigned(EditorPropertyInteger *p_editor) { return p_editor->is_unsigned; }
	static int64_t get_signed_min(EditorPropertyInteger *p_editor) { return p_editor->signed_min; }
	static int64_t get_signed_max(EditorPropertyInteger *p_editor) { return p_editor->signed_max; }
	static uint64_t get_unsigned_min(EditorPropertyInteger *p_editor) { return p_editor->unsigned_min; }
	static uint64_t get_unsigned_max(EditorPropertyInteger *p_editor) { return p_editor->unsigned_max; }
	static bool get_slider_bounds_exact(EditorPropertyInteger *p_editor) { return p_editor->slider_bounds_exact; }

	// Simulates typing text into the control and pressing enter.
	static void submit_text(EditorPropertyInteger *p_editor, const String &p_text) {
		p_editor->value_edit->set_text(p_text);
		p_editor->_text_submitted(p_text);
	}
};

namespace TestEditorExactIntegerProperty {

// `callable_mp` requires an `Object`-derived target, so the capture is a plain stack-allocated
// `Object` rather than a bare struct.
class PropertyChangeCapture : public Object {
	FOUNDRY_CLASS(PropertyChangeCapture, Object);

public:
	int count = 0;
	Variant value;
	Variant::Type carrier = Variant::NIL;

	void on_property_changed(const StringName &p_property, const Variant &p_value, const StringName &p_field, bool p_changing) {
		count++;
		value = p_value;
		carrier = p_value.get_type();
	}
};

// `Object::set/get` only routes through `ClassDB` for a registered class, so the fixture class is
// registered once (idempotently) before it is instantiated.
static void ensure_fixture_registered() {
	if (!ClassDB::class_exists("ExactIntegerPropertyFixtureObject")) {
		FOUNDRY_REGISTER_CLASS(ExactIntegerPropertyFixtureObject);
	}
}

TEST_CASE("[SceneTree][Editor] Default plugin routes Variant::INT and Variant::UINT to the exact integer editor") {
	EditorProperty *int_editor = EditorInspectorDefaultPlugin::get_editor_for_property(nullptr, Variant::INT, "signed_value", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_DEFAULT, false);
	REQUIRE(int_editor != nullptr);
	EditorPropertyInteger *int_property = Object::cast_to<EditorPropertyInteger>(int_editor);
	REQUIRE(int_property != nullptr);
	CHECK_FALSE(EditorPropertyIntegerTestAccess::get_is_unsigned(int_property));
	memdelete(int_editor);

	EditorProperty *uint_editor = EditorInspectorDefaultPlugin::get_editor_for_property(nullptr, Variant::UINT, "unsigned_value", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_DEFAULT, false);
	REQUIRE(uint_editor != nullptr);
	EditorPropertyInteger *uint_property = Object::cast_to<EditorPropertyInteger>(uint_editor);
	REQUIRE(uint_property != nullptr);
	CHECK(EditorPropertyIntegerTestAccess::get_is_unsigned(uint_property));
	memdelete(uint_editor);
}

TEST_CASE("[SceneTree][Editor] Signed exact integer editor round-trips INT64_MIN, INT64_MAX, and zero") {
	ensure_fixture_registered();
	Ref<ExactIntegerPropertyFixtureObject> object = memnew(ExactIntegerPropertyFixtureObject);
	EditorPropertyInteger *editor = memnew(EditorPropertyInteger);
	editor->setup(EditorPropertyRangeHint());
	editor->set_object_and_property(object.ptr(), "signed_value");

	PropertyChangeCapture capture;
	editor->connect("property_changed", callable_mp(&capture, &PropertyChangeCapture::on_property_changed));

	struct Case {
		const char *text;
		int64_t expected;
	};
	const Case cases[] = {
		{ "-9223372036854775808", INT64_MIN },
		{ "9223372036854775807", INT64_MAX },
		{ "0", 0 },
	};

	for (const Case &c : cases) {
		capture.count = 0;
		EditorPropertyIntegerTestAccess::submit_text(editor, c.text);
		REQUIRE(capture.count == 1);
		CHECK(capture.carrier == Variant::INT);
		CHECK(int64_t(capture.value) == c.expected);
		object->set_signed_value(c.expected);
		editor->update_property();
	}

	memdelete(editor);
}

TEST_CASE("[SceneTree][Editor] Unsigned exact integer editor round-trips UINT64_MAX and zero") {
	ensure_fixture_registered();
	Ref<ExactIntegerPropertyFixtureObject> object = memnew(ExactIntegerPropertyFixtureObject);
	EditorPropertyRangeHint hint;
	hint.is_unsigned_integer = true;
	EditorPropertyInteger *editor = memnew(EditorPropertyInteger);
	editor->setup(hint);
	editor->set_object_and_property(object.ptr(), "unsigned_value");

	PropertyChangeCapture capture;
	editor->connect("property_changed", callable_mp(&capture, &PropertyChangeCapture::on_property_changed));

	struct Case {
		const char *text;
		uint64_t expected;
	};
	const Case cases[] = {
		{ "18446744073709551615", UINT64_MAX },
		{ "0", 0 },
	};

	for (const Case &c : cases) {
		capture.count = 0;
		EditorPropertyIntegerTestAccess::submit_text(editor, c.text);
		REQUIRE(capture.count == 1);
		CHECK(capture.carrier == Variant::UINT);
		CHECK(uint64_t(capture.value) == c.expected);
		object->set_unsigned_value(c.expected);
		editor->update_property();
	}

	memdelete(editor);
}

TEST_CASE("[SceneTree][Editor] Exact integer editor rejects invalid, negative-unsigned, and out-of-range input atomically") {
	ensure_fixture_registered();
	Ref<ExactIntegerPropertyFixtureObject> object = memnew(ExactIntegerPropertyFixtureObject);
	object->set_signed_value(42);

	EditorPropertyRangeHint hint;
	hint.or_greater = false;
	hint.or_less = false;
	hint.exact_int_min = 0;
	hint.exact_int_max = 100;
	EditorPropertyInteger *signed_editor = memnew(EditorPropertyInteger);
	signed_editor->setup(hint);
	signed_editor->set_object_and_property(object.ptr(), "signed_value");
	signed_editor->update_property();

	PropertyChangeCapture signed_capture;
	signed_editor->connect("property_changed", callable_mp(&signed_capture, &PropertyChangeCapture::on_property_changed));

	// Invalid text.
	EditorPropertyIntegerTestAccess::submit_text(signed_editor, "not a number");
	CHECK(signed_capture.count == 0);
	CHECK(EditorPropertyIntegerTestAccess::get_value_edit(signed_editor)->get_text() == "42");

	// One past the configured range.
	EditorPropertyIntegerTestAccess::submit_text(signed_editor, "101");
	CHECK(signed_capture.count == 0);
	CHECK(EditorPropertyIntegerTestAccess::get_value_edit(signed_editor)->get_text() == "42");

	memdelete(signed_editor);

	ensure_fixture_registered();
	Ref<ExactIntegerPropertyFixtureObject> unsigned_object = memnew(ExactIntegerPropertyFixtureObject);
	unsigned_object->set_unsigned_value(7);

	EditorPropertyRangeHint unsigned_hint;
	unsigned_hint.is_unsigned_integer = true;
	EditorPropertyInteger *unsigned_editor = memnew(EditorPropertyInteger);
	unsigned_editor->setup(unsigned_hint);
	unsigned_editor->set_object_and_property(unsigned_object.ptr(), "unsigned_value");
	unsigned_editor->update_property();

	PropertyChangeCapture unsigned_capture;
	unsigned_editor->connect("property_changed", callable_mp(&unsigned_capture, &PropertyChangeCapture::on_property_changed));

	// Negative input on an unsigned control must not emit a change.
	EditorPropertyIntegerTestAccess::submit_text(unsigned_editor, "-1");
	CHECK(unsigned_capture.count == 0);
	CHECK(EditorPropertyIntegerTestAccess::get_value_edit(unsigned_editor)->get_text() == "7");

	// One past UINT64_MAX; a naive parser could silently wrap this instead of rejecting it.
	EditorPropertyIntegerTestAccess::submit_text(unsigned_editor, "18446744073709551616");
	CHECK(unsigned_capture.count == 0);
	CHECK(EditorPropertyIntegerTestAccess::get_value_edit(unsigned_editor)->get_text() == "7");

	memdelete(unsigned_editor);
}

TEST_CASE("[SceneTree][Editor] Exact integer editor does not emit a redundant change when the committed value is unchanged") {
	ensure_fixture_registered();
	Ref<ExactIntegerPropertyFixtureObject> object = memnew(ExactIntegerPropertyFixtureObject);
	object->set_signed_value(42);

	EditorPropertyInteger *editor = memnew(EditorPropertyInteger);
	editor->setup(EditorPropertyRangeHint());
	editor->set_object_and_property(object.ptr(), "signed_value");
	editor->update_property();

	PropertyChangeCapture capture;
	editor->connect("property_changed", callable_mp(&capture, &PropertyChangeCapture::on_property_changed));

	// Re-submitting the already-committed value (e.g. clicking into the field and away again, or
	// pressing enter without editing) must not emit a property change or trigger a spurious undo step.
	EditorPropertyIntegerTestAccess::submit_text(editor, "42");
	CHECK(capture.count == 0);
	CHECK(EditorPropertyIntegerTestAccess::get_value_edit(editor)->get_text() == "42");

	// A non-canonical spelling of the same value (leading zeros) is still a no-op for the property,
	// but the field is reformatted back to the canonical form.
	EditorPropertyIntegerTestAccess::submit_text(editor, "042");
	CHECK(capture.count == 0);
	CHECK(EditorPropertyIntegerTestAccess::get_value_edit(editor)->get_text() == "42");

	// A genuinely different value still emits exactly once.
	EditorPropertyIntegerTestAccess::submit_text(editor, "43");
	CHECK(capture.count == 1);
	CHECK(int64_t(capture.value) == 43);

	memdelete(editor);
}

TEST_CASE("[SceneTree][Editor] Integer range hint bounds and step parse exactly without a double round-trip") {
	EditorProperty *editor = EditorInspectorDefaultPlugin::get_editor_for_property(
			nullptr, Variant::UINT, "unsigned_value", PROPERTY_HINT_RANGE, "0,18446744073709551615,1", PROPERTY_USAGE_DEFAULT, false);
	REQUIRE(editor != nullptr);
	EditorPropertyInteger *uint_property = Object::cast_to<EditorPropertyInteger>(editor);
	REQUIRE(uint_property != nullptr);
	CHECK(EditorPropertyIntegerTestAccess::get_unsigned_min(uint_property) == 0);
	CHECK(EditorPropertyIntegerTestAccess::get_unsigned_max(uint_property) == UINT64_MAX);
	memdelete(editor);

	EditorProperty *signed_editor = EditorInspectorDefaultPlugin::get_editor_for_property(
			nullptr, Variant::INT, "signed_value", PROPERTY_HINT_RANGE, "-9223372036854775808,9223372036854775807,1", PROPERTY_USAGE_DEFAULT, false);
	REQUIRE(signed_editor != nullptr);
	EditorPropertyInteger *int_property = Object::cast_to<EditorPropertyInteger>(signed_editor);
	REQUIRE(int_property != nullptr);
	CHECK(EditorPropertyIntegerTestAccess::get_signed_min(int_property) == INT64_MIN);
	CHECK(EditorPropertyIntegerTestAccess::get_signed_max(int_property) == INT64_MAX);
	memdelete(signed_editor);
}

TEST_CASE("[SceneTree][Editor] The slider only appears when value, bounds, and step are exactly representable in double") {
	// The default (unbounded) 64-bit range cannot be represented exactly in a double, so the
	// slider must stay hidden even though a small current value would be exact on its own.
	EditorProperty *unbounded = EditorInspectorDefaultPlugin::get_editor_for_property(
			nullptr, Variant::INT, "signed_value", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_DEFAULT, false);
	EditorPropertyInteger *unbounded_property = Object::cast_to<EditorPropertyInteger>(unbounded);
	REQUIRE(unbounded_property != nullptr);
	CHECK_FALSE(EditorPropertyIntegerTestAccess::get_slider_bounds_exact(unbounded_property));
	CHECK_FALSE(EditorPropertyIntegerTestAccess::get_spin(unbounded_property)->is_visible());
	memdelete(unbounded);

	// A small explicit range is exactly representable, so the slider is retained.
	EditorProperty *bounded = EditorInspectorDefaultPlugin::get_editor_for_property(
			nullptr, Variant::INT, "signed_value", PROPERTY_HINT_RANGE, "0,100,1", PROPERTY_USAGE_DEFAULT, false);
	EditorPropertyInteger *bounded_property = Object::cast_to<EditorPropertyInteger>(bounded);
	REQUIRE(bounded_property != nullptr);
	CHECK(EditorPropertyIntegerTestAccess::get_slider_bounds_exact(bounded_property));
	memdelete(bounded);

	// A range that spans UINT64_MAX is never exactly representable, so the slider stays hidden
	// no matter how the current value is set.
	EditorProperty *huge_range = EditorInspectorDefaultPlugin::get_editor_for_property(
			nullptr, Variant::UINT, "unsigned_value", PROPERTY_HINT_RANGE, "0,18446744073709551615,1", PROPERTY_USAGE_DEFAULT, false);
	EditorPropertyInteger *huge_range_property = Object::cast_to<EditorPropertyInteger>(huge_range);
	REQUIRE(huge_range_property != nullptr);
	CHECK_FALSE(EditorPropertyIntegerTestAccess::get_slider_bounds_exact(huge_range_property));
	memdelete(huge_range);
}

} // namespace TestEditorExactIntegerProperty
