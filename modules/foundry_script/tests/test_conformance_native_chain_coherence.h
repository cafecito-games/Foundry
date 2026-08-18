/**************************************************************************/
/*  test_conformance_native_chain_coherence.h                             */
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
#include "modules/foundry_script/fs_conformance_registry.h"
#include "modules/foundry_script/fs_parser.h"
#include "modules/foundry_script/fs_trait_utils.h"

#include "tests/test_macros.h"

// A retroactive conformance on an engine class also answers for every subclass of that class, so two
// `extend` declarations along one ClassDB chain describe the same trait for overlapping receivers.
// Binding the trait to different type arguments on the two levels is the ClassDB analog of
// re-applying a trait with different arguments on a subclass, and is rejected for the same reason:
// widening a receiver to the ancestor's type would otherwise switch which arguments apply, letting a
// call typed against one argument list dispatch a witness written for the other.
namespace FSNativeChainCoherenceTests {

// Analyzes one source under a stable path and drops whatever it registered again, so a case states
// only the declarations it is about. Registration is a process-global side effect of analysis.
class NativeChainFixture {
	FSParser parser;

public:
	static constexpr const char *SOURCE_PATH = "user://native_chain_coherence.fs";

	Vector<String> error_messages;

	explicit NativeChainFixture(const String &p_source) {
		REQUIRE_EQ(parser.parse(p_source, SOURCE_PATH, false), OK);
		FSAnalyzer analyzer(&parser);
		analyzer.analyze();
		for (const FSParser::ParserError &error : parser.get_errors()) {
			error_messages.push_back(error.message);
		}
	}

	~NativeChainFixture() {
		FSConformanceRegistry::get_singleton()->clear_file(SOURCE_PATH);
	}

	// A trait declared without `trait_name` is identified by its file-scoped fully-qualified name, so
	// a registry lookup has to ask the parse tree for it rather than assume the written spelling.
	StringName trait_identity(const StringName &p_name) {
		FSParser::ClassNode *tree = parser.get_tree();
		REQUIRE(tree != nullptr);
		for (const FSParser::ClassNode::Member &member : tree->members) {
			if (member.type == FSParser::ClassNode::Member::CLASS && member.m_class != nullptr &&
					member.m_class->identifier != nullptr && member.m_class->identifier->name == p_name) {
				return fs_trait_identity_name(member.m_class);
			}
		}
		FAIL("trait not found in the parse tree");
		return StringName();
	}

	bool has_error_containing(const String &p_fragment) const {
		for (const String &message : error_messages) {
			if (message.contains(p_fragment)) {
				return true;
			}
		}
		return false;
	}
};

TEST_CASE("[Modules][FoundryScript][Conformance] Conflicting trait arguments on one native chain are rejected") {
	NativeChainFixture fixture(R"(
trait NccKeeper[T]:
	abstract func make() -> T


extend Object uses NccKeeper[int]:
	func make() -> int:
		return 7


extend RefCounted uses NccKeeper[String]:
	func make() -> String:
		return "seven"


func test() -> void:
	pass
)");

	CHECK(fixture.has_error_containing("is already applied with different type arguments"));
	CHECK(fixture.has_error_containing("NccKeeper"));
	CHECK(fixture.has_error_containing("Object"));
}

TEST_CASE("[Modules][FoundryScript][Conformance] The native chain rule does not depend on declaration order") {
	NativeChainFixture fixture(R"(
trait NccOrderKeeper[T]:
	abstract func make() -> T


extend RefCounted uses NccOrderKeeper[String]:
	func make() -> String:
		return "seven"


extend Object uses NccOrderKeeper[int]:
	func make() -> int:
		return 7


func test() -> void:
	pass
)");

	CHECK(fixture.has_error_containing("is already applied with different type arguments"));
}

TEST_CASE("[Modules][FoundryScript][Conformance] Matching trait arguments on one native chain are accepted") {
	NativeChainFixture fixture(R"(
trait NccAgreeKeeper[T]:
	abstract func make() -> T


extend Object uses NccAgreeKeeper[int]:
	func make() -> int:
		return 7


extend RefCounted uses NccAgreeKeeper[int]:
	func make() -> int:
		return 11


func test() -> void:
	pass
)");

	CHECK(fixture.error_messages.is_empty());
}

TEST_CASE("[Modules][FoundryScript][Conformance] Native classes on different branches may bind a trait differently") {
	NativeChainFixture fixture(R"(
trait NccBranchKeeper[T]:
	abstract func make() -> T


extend RefCounted uses NccBranchKeeper[int]:
	func make() -> int:
		return 7


extend Node uses NccBranchKeeper[String]:
	func make() -> String:
		return "seven"


func test() -> void:
	pass
)");

	// Neither class derives from the other, so no value ever reaches both conformances.
	CHECK(fixture.error_messages.is_empty());
}

TEST_CASE("[Modules][FoundryScript][Conformance] A conformance that supplies no trait arguments is not evidence") {
	NativeChainFixture fixture(R"(
trait NccOpenKeeper[T]:
	abstract func size() -> int


extend Object uses NccOpenKeeper:
	func size() -> int:
		return 0


extend RefCounted uses NccOpenKeeper[String]:
	func size() -> int:
		return 1


func test() -> void:
	pass
)");

	CHECK(fixture.error_messages.is_empty());
}

TEST_CASE("[Modules][FoundryScript][Conformance] Only engine-class targets are reported as native conformances") {
	NativeChainFixture fixture(R"(
trait NccRecordKeeper[T]:
	abstract func make() -> T


class NccHolder:
	pass


extend RefCounted uses NccRecordKeeper[int]:
	func make() -> int:
		return 7


extend String uses NccRecordKeeper[String]:
	func make() -> String:
		return self


extend NccHolder uses NccRecordKeeper[int]:
	func make() -> int:
		return 11


func test() -> void:
	pass
)");
	REQUIRE(fixture.error_messages.is_empty());

	const Vector<FSConformanceRegistry::NativeConformanceRecord> records =
			FSConformanceRegistry::get_singleton()->get_native_conformance_records(
					fixture.trait_identity(StringName("NccRecordKeeper")));
	// Neither a builtin value type nor a Foundry Script class is an engine class, so neither may be
	// mistaken for one in the engine-to-engine half of the rule. A script class still reaches the
	// engine chain through its terminal base, which the script-target half compares separately, so all
	// three declarations here agree on the arguments.
	REQUIRE_EQ(records.size(), 1);
	CHECK_EQ(records[0].native_class, StringName("RefCounted"));
	CHECK_EQ(records[0].source_file, String(NativeChainFixture::SOURCE_PATH));
	CHECK_EQ(records[0].trait_type_arguments.size(), 1);
}

// A script class's chain does not stop at its last script base: it continues through the engine class
// that base ends on. A retroactive conformance on that engine ancestry answers for the script class's
// receivers too, so the two declarations bind one trait for overlapping values and must agree.
TEST_CASE("[Modules][FoundryScript][Conformance] A script class conflicts with its native ancestor's conformance") {
	NativeChainFixture fixture(R"(
trait NccScriptKeeper[T]:
	abstract func make() -> T


class NccScriptHolder extends RefCounted:
	pass


extend RefCounted uses NccScriptKeeper[int]:
	func make() -> int:
		return 7


extend NccScriptHolder uses NccScriptKeeper[String]:
	func make() -> String:
		return "seven"


func test() -> void:
	pass
)");

	CHECK(fixture.has_error_containing("is already applied with different type arguments"));
	CHECK(fixture.has_error_containing("NccScriptKeeper"));
	CHECK(fixture.has_error_containing("RefCounted"));
}

TEST_CASE("[Modules][FoundryScript][Conformance] The script chain rule does not depend on declaration order") {
	NativeChainFixture fixture(R"(
trait NccScriptOrderKeeper[T]:
	abstract func make() -> T


class NccScriptOrderHolder extends RefCounted:
	pass


extend NccScriptOrderHolder uses NccScriptOrderKeeper[String]:
	func make() -> String:
		return "seven"


extend RefCounted uses NccScriptOrderKeeper[int]:
	func make() -> int:
		return 7


func test() -> void:
	pass
)");

	CHECK(fixture.has_error_containing("is already applied with different type arguments"));
	CHECK(fixture.has_error_containing("NccScriptOrderKeeper"));
	CHECK(fixture.has_error_containing("NccScriptOrderHolder"));
}

TEST_CASE("[Modules][FoundryScript][Conformance] A rejected script-chain conformance registers nothing") {
	NativeChainFixture fixture(R"(
trait NccRejectedKeeper[T]:
	abstract func make() -> T


class NccRejectedHolder extends RefCounted:
	pass


extend RefCounted uses NccRejectedKeeper[int]:
	func make() -> int:
		return 7


extend NccRejectedHolder uses NccRejectedKeeper[String]:
	func make() -> String:
		return "seven"


func test() -> void:
	pass
)");
	REQUIRE(fixture.has_error_containing("is already applied with different type arguments"));

	// The engine declaration is coherent on its own and stays; only the contradicting one is dropped,
	// so no witness or membership is published for a conformance the program was rejected over.
	const StringName identity = fixture.trait_identity(StringName("NccRejectedKeeper"));
	const Vector<FSConformanceRegistry::ScriptConformanceRecord> script_records =
			FSConformanceRegistry::get_singleton()->get_script_conformance_records(identity);
	CHECK(script_records.is_empty());
	const Vector<FSConformanceRegistry::NativeConformanceRecord> native_records =
			FSConformanceRegistry::get_singleton()->get_native_conformance_records(identity);
	CHECK_EQ(native_records.size(), 1);
}

TEST_CASE("[Modules][FoundryScript][Conformance] A script class may repeat its native ancestor's arguments") {
	NativeChainFixture fixture(R"(
trait NccScriptAgreeKeeper[T]:
	abstract func make() -> T


class NccScriptAgreeHolder extends RefCounted:
	pass


extend RefCounted uses NccScriptAgreeKeeper[int]:
	func make() -> int:
		return 7


extend NccScriptAgreeHolder uses NccScriptAgreeKeeper[int]:
	func make() -> int:
		return 11


func test() -> void:
	pass
)");

	CHECK(fixture.error_messages.is_empty());
}

TEST_CASE("[Modules][FoundryScript][Conformance] A script class on another native branch may bind a trait differently") {
	NativeChainFixture fixture(R"(
trait NccBranchScriptKeeper[T]:
	abstract func make() -> T


class NccBranchScriptHolder extends Node:
	pass


extend RefCounted uses NccBranchScriptKeeper[int]:
	func make() -> int:
		return 7


extend NccBranchScriptHolder uses NccBranchScriptKeeper[String]:
	func make() -> String:
		return "seven"


func test() -> void:
	pass
)");

	// A `Node` is never a `RefCounted`, so no value reaches both conformances.
	CHECK(fixture.error_messages.is_empty());
}

TEST_CASE("[Modules][FoundryScript][Conformance] A script class with no conforming native ancestor is unaffected") {
	NativeChainFixture fixture(R"(
trait NccLoneKeeper[T]:
	abstract func make() -> T


class NccLoneHolder extends RefCounted:
	pass


extend NccLoneHolder uses NccLoneKeeper[String]:
	func make() -> String:
		return "seven"


func test() -> void:
	pass
)");

	CHECK(fixture.error_messages.is_empty());
}

TEST_CASE("[Modules][FoundryScript][Conformance] A native conformance without arguments is not evidence for a script class") {
	NativeChainFixture fixture(R"(
trait NccOpenScriptKeeper[T]:
	abstract func size() -> int


class NccOpenScriptHolder extends RefCounted:
	pass


extend RefCounted uses NccOpenScriptKeeper:
	func size() -> int:
		return 0


extend NccOpenScriptHolder uses NccOpenScriptKeeper[String]:
	func size() -> int:
		return 1


func test() -> void:
	pass
)");

	CHECK(fixture.error_messages.is_empty());
}

TEST_CASE("[Modules][FoundryScript][Conformance] A concrete composite position still conflicts across a script chain") {
	NativeChainFixture fixture(R"(
trait NccCompositeKeeper[T]:
	abstract func make() -> T


class NccCompositeHolder extends RefCounted:
	pass


extend RefCounted uses NccCompositeKeeper[Array[int]]:
	func make() -> Array[int]:
		return [7]


extend NccCompositeHolder uses NccCompositeKeeper[Array[String]]:
	func make() -> Array[String]:
		return ["seven"]


func test() -> void:
	pass
)");

	CHECK(fixture.has_error_containing("is already applied with different type arguments"));
}

TEST_CASE("[Modules][FoundryScript][Conformance] A class using a trait cannot contradict its native ancestor's conformance") {
	NativeChainFixture fixture(R"(
trait NccUsesKeeper[T]:
	abstract func make() -> T


extend RefCounted uses NccUsesKeeper[int]:
	func make() -> int:
		return 7


class NccUsesHolder extends RefCounted uses NccUsesKeeper[String]:
	func make() -> String:
		return "seven"


func test() -> void:
	pass
)");

	CHECK(fixture.has_error_containing("is already applied with different type arguments"));
	CHECK(fixture.has_error_containing("NccUsesKeeper"));
}

TEST_CASE("[Modules][FoundryScript][Conformance] An implied supertrait identity is checked across a script chain") {
	NativeChainFixture fixture(R"(
trait NccBaseKeeper[T]:
	abstract func make() -> T


trait NccDerivedKeeper[T] uses NccBaseKeeper[T]:
	abstract func label() -> String


class NccImpliedHolder extends RefCounted:
	pass


extend RefCounted uses NccBaseKeeper[int]:
	func make() -> int:
		return 7


extend NccImpliedHolder uses NccDerivedKeeper[String]:
	func make() -> String:
		return "seven"

	func label() -> String:
		return "holder"


func test() -> void:
	pass
)");

	// The conflict is on the implied `NccBaseKeeper` identity, not on the trait the declaration names.
	CHECK(fixture.has_error_containing("is already applied with different type arguments"));
	CHECK(fixture.has_error_containing("NccBaseKeeper"));
}

} // namespace FSNativeChainCoherenceTests
