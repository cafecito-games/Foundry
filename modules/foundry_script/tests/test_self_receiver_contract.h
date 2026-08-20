/**************************************************************************/
/*  test_self_receiver_contract.h                                         */
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

#include "modules/foundry_script/fs_analyzer.h"
#include "modules/foundry_script/fs_parser.h"

#include "tests/test_macros.h"

// Coverage for the receiver-contract stamp: a receiver expression already typed as the frame's
// `@Self` *is* that `Self`, so stamping a parameter contract from it has to reuse its binding. If it
// nested instead, the parameter would carry `@Self bound by (@Self bound by C)` while every `Self` a
// caller can write carries `@Self bound by C`, and the `self.`-qualified spelling of a call would
// stop type-checking like its unqualified spelling.

namespace FSTests {

static FSParser::DataType self_contract_native_type(const StringName &p_native_type) {
	FSParser::DataType type;
	type.kind = FSParser::DataType::NATIVE;
	type.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;
	type.native_type = p_native_type;
	return type;
}

TEST_CASE("[Modules][FoundryScript][SelfContract] stamping a receiver contract binds an ordinary receiver type") {
	const FSParser::DataType receiver = self_contract_native_type(SNAME("RefCounted"));
	const FSParser::DataType stamped = FSAnalyzer::test_self_type_parameter_from_bound(receiver);
	CHECK(stamped.kind == FSParser::DataType::TYPE_PARAMETER);
	CHECK(stamped.type_parameter_name == StringName("@Self"));
	CHECK(stamped.type_parameter_scope == FSParser::DataType::TYPE_PARAMETER_CLASS);
	REQUIRE(stamped.type_parameter_bound.size() == 1);
	CHECK(stamped.type_parameter_bound[0] == receiver);
}

TEST_CASE("[Modules][FoundryScript][SelfContract] stamping a receiver contract is idempotent over a Self-typed receiver") {
	const FSParser::DataType receiver = self_contract_native_type(SNAME("RefCounted"));
	const FSParser::DataType frame_self = FSAnalyzer::test_self_type_parameter_from_bound(receiver);
	// Restamping models a `self.`-qualified call or a member-callable capture, whose receiver
	// expression is itself typed `Self`.
	const FSParser::DataType restamped = FSAnalyzer::test_self_type_parameter_from_bound(frame_self);
	CHECK(restamped == frame_self);
	REQUIRE(restamped.type_parameter_bound.size() == 1);
	CHECK(restamped.type_parameter_bound[0].kind != FSParser::DataType::TYPE_PARAMETER);
	// A further stamp, the alias-of-an-alias spelling, cannot deepen the bound either.
	CHECK(FSAnalyzer::test_self_type_parameter_from_bound(restamped) == frame_self);
}

TEST_CASE("[Modules][FoundryScript][SelfContract] stamping a receiver contract binds a Self type handle rather than reusing it") {
	// `Type[Self]` denotes the class handle, not an instance of the frame's `Self`, so it is an
	// ordinary bound and the reuse must not swallow it.
	const FSParser::DataType receiver = self_contract_native_type(SNAME("RefCounted"));
	FSParser::DataType handle = FSAnalyzer::test_self_type_parameter_from_bound(receiver);
	handle.is_type_handle_annotation = true;
	const FSParser::DataType stamped = FSAnalyzer::test_self_type_parameter_from_bound(handle);
	REQUIRE(stamped.type_parameter_bound.size() == 1);
	CHECK(stamped.type_parameter_bound[0] == handle);
}

} //namespace FSTests
