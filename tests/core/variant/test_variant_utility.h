/**************************************************************************/
/*  test_variant_utility.h                                                */
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

#include "core/variant/variant_utility.h"

#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/object/script_diagnostic_capture.h"
#include "core/os/os.h"

#include "tests/test_macros.h"

namespace TestVariantUtility {

TEST_CASE("[VariantUtility] Type conversion") {
	Variant converted;
	converted = VariantUtilityFunctions::type_convert("Hi!", Variant::Type::NIL);
	CHECK(converted.get_type() == Variant::Type::NIL);
	CHECK(converted == Variant());

	converted = VariantUtilityFunctions::type_convert("Hi!", Variant::Type::INT);
	CHECK(converted.get_type() == Variant::Type::INT);
	CHECK(converted == Variant(0));

	converted = VariantUtilityFunctions::type_convert("123", Variant::Type::INT);
	CHECK(converted.get_type() == Variant::Type::INT);
	CHECK(converted == Variant(123));

	converted = VariantUtilityFunctions::type_convert(123, Variant::Type::STRING);
	CHECK(converted.get_type() == Variant::Type::STRING);
	CHECK(converted == Variant("123"));

	converted = VariantUtilityFunctions::type_convert(123.4, Variant::Type::INT);
	CHECK(converted.get_type() == Variant::Type::INT);
	CHECK(converted == Variant(123));

	converted = VariantUtilityFunctions::type_convert(5, Variant::Type::VECTOR2);
	CHECK(converted.get_type() == Variant::Type::VECTOR2);
	CHECK(converted == Variant(Vector2(0, 0)));

	converted = VariantUtilityFunctions::type_convert(Vector3(1, 2, 3), Variant::Type::VECTOR2);
	CHECK(converted.get_type() == Variant::Type::VECTOR2);
	CHECK(converted == Variant(Vector2(1, 2)));

	converted = VariantUtilityFunctions::type_convert(Vector2(1, 2), Variant::Type::VECTOR4);
	CHECK(converted.get_type() == Variant::Type::VECTOR4);
	CHECK(converted == Variant(Vector4(1, 2, 0, 0)));

	converted = VariantUtilityFunctions::type_convert(Vector4(1.2, 3.4, 5.6, 7.8), Variant::Type::VECTOR3I);
	CHECK(converted.get_type() == Variant::Type::VECTOR3I);
	CHECK(converted == Variant(Vector3i(1, 3, 5)));

	{
		Basis basis = Basis::from_scale(Vector3(1.2, 3.4, 5.6));
		Transform3D transform = Transform3D(basis, Vector3());

		converted = VariantUtilityFunctions::type_convert(transform, Variant::Type::BASIS);
		CHECK(converted.get_type() == Variant::Type::BASIS);
		CHECK(converted == basis);

		converted = VariantUtilityFunctions::type_convert(basis, Variant::Type::TRANSFORM3D);
		CHECK(converted.get_type() == Variant::Type::TRANSFORM3D);
		CHECK(converted == transform);

		converted = VariantUtilityFunctions::type_convert(basis, Variant::Type::STRING);
		CHECK(converted.get_type() == Variant::Type::STRING);
		CHECK(converted == Variant("[X: (1.2, 0.0, 0.0), Y: (0.0, 3.4, 0.0), Z: (0.0, 0.0, 5.6)]"));
	}

	{
		Array arr = { 1.2, 3.4, 5.6 };
		PackedFloat64Array packed = { 1.2, 3.4, 5.6 };

		converted = VariantUtilityFunctions::type_convert(arr, Variant::Type::PACKED_FLOAT64_ARRAY);
		CHECK(converted.get_type() == Variant::Type::PACKED_FLOAT64_ARRAY);
		CHECK(converted == packed);

		converted = VariantUtilityFunctions::type_convert(packed, Variant::Type::ARRAY);
		CHECK(converted.get_type() == Variant::Type::ARRAY);
		CHECK(converted == arr);
	}

	{
		// Check that using Variant::call_utility_function also works.
		Vector<const Variant *> args;
		Variant data_arg = "Hi!";
		args.push_back(&data_arg);
		Variant type_arg = Variant::Type::NIL;
		args.push_back(&type_arg);
		Callable::CallError call_error;
		Variant::call_utility_function("type_convert", &converted, (const Variant **)args.ptr(), 2, call_error);
		CHECK(converted.get_type() == Variant::Type::NIL);
		CHECK(converted == Variant());

		type_arg = Variant::Type::INT;
		Variant::call_utility_function("type_convert", &converted, (const Variant **)args.ptr(), 2, call_error);
		CHECK(converted.get_type() == Variant::Type::INT);
		CHECK(converted == Variant(0));

		data_arg = "123";
		Variant::call_utility_function("type_convert", &converted, (const Variant **)args.ptr(), 2, call_error);
		CHECK(converted.get_type() == Variant::Type::INT);
		CHECK(converted == Variant(123));
	}
}

TEST_CASE("[VariantUtility] push_fatal decision logic") {
	OS *os = OS::get_singleton();
	Engine *engine = Engine::get_singleton();
	ProjectSettings *settings = ProjectSettings::get_singleton();

	// Save state we will mutate so the test does not leak.
	const int saved_exit_code = os->get_exit_code();
	const bool saved_exit_requested = os->is_exit_requested();
	const bool saved_editor_hint = engine->is_editor_hint();
	settings->set_setting("application/run/push_fatal_terminates", true);

	Variant message = "fatal!";
	const Variant *args[1] = { &message };
	Variant ret;
	Callable::CallError call_error;

	// Case 1: too few arguments -> error, no exit request.
	{
		const bool before = os->is_exit_requested();
		Variant::call_utility_function("push_fatal", &ret, nullptr, 0, call_error);
		CHECK(call_error.error == Callable::CallError::CALL_ERROR_TOO_FEW_ARGUMENTS);
		CHECK(os->is_exit_requested() == before);
	}

	// Case 2: editor hint on -> log only, no exit request.
	{
		engine->set_editor_hint(true);
		settings->set_setting("application/run/push_fatal_terminates", true);
		const bool before = os->is_exit_requested();
		Variant::call_utility_function("push_fatal", &ret, args, 1, call_error);
		CHECK(call_error.error == Callable::CallError::CALL_OK);
		CHECK(os->is_exit_requested() == before);
		engine->set_editor_hint(false);
	}

	// Case 3: setting disabled -> log only, no exit request.
	{
		settings->set_setting("application/run/push_fatal_terminates", false);
		const bool before = os->is_exit_requested();
		Variant::call_utility_function("push_fatal", &ret, args, 1, call_error);
		CHECK(call_error.error == Callable::CallError::CALL_OK);
		CHECK(os->is_exit_requested() == before);
	}

	// Case 4: not editor, setting enabled -> request exit with EXIT_FAILURE.
	{
		settings->set_setting("application/run/push_fatal_terminates", true);
		Variant::call_utility_function("push_fatal", &ret, args, 1, call_error);
		CHECK(call_error.error == Callable::CallError::CALL_OK);
		CHECK(os->is_exit_requested());
		CHECK(os->get_exit_code() == EXIT_FAILURE);
	}

	// Case 5: active capture -> log only, no exit request, even with the setting enabled.
	{
		settings->set_setting("application/run/push_fatal_terminates", true);
		os->clear_exit_request();

		Ref<ScriptDiagnosticCapture> capture;
		capture.instantiate();
		capture->start();

		Variant::call_utility_function("push_fatal", &ret, args, 1, call_error);
		CHECK(call_error.error == Callable::CallError::CALL_OK);
		CHECK_FALSE(os->is_exit_requested());
		CHECK(capture->has_fatal("fatal!"));

		capture->stop();
	}

	// Restore mutated state, including the process-global exit-request flag
	// that Case 4 trips, so this test does not leak into others.
	engine->set_editor_hint(saved_editor_hint);
	settings->set_setting("application/run/push_fatal_terminates", true);
	if (saved_exit_requested) {
		os->request_exit(saved_exit_code);
	} else {
		os->clear_exit_request();
		os->set_exit_code(saved_exit_code);
	}
}

} // namespace TestVariantUtility
