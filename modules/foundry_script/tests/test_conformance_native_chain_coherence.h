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

	// A class's fully-qualified name is derived from the file it was parsed under, so a registry lookup
	// that must not match a same-named class elsewhere reads it from the parse tree.
	String class_fqcn(const StringName &p_name) {
		FSParser::ClassNode *tree = parser.get_tree();
		REQUIRE(tree != nullptr);
		for (const FSParser::ClassNode::Member &member : tree->members) {
			if (member.type == FSParser::ClassNode::Member::CLASS && member.m_class != nullptr &&
					member.m_class->identifier != nullptr && member.m_class->identifier->name == p_name) {
				return member.m_class->fqcn;
			}
		}
		FAIL("class not found in the parse tree");
		return String();
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

// One semantic chain does not begin at the engine ancestry: it runs from a script class through every
// script base above it. A retroactive conformance on a script ancestor answers for the descendant's
// receivers too, so the two declarations bind one trait for overlapping values and must agree, exactly
// as they must on the engine half of the same chain.
TEST_CASE("[Modules][FoundryScript][Conformance] A conformance conflicts with its script ancestor's conformance") {
	NativeChainFixture fixture(R"(
trait NccAncestorKeeper[T]:
	abstract func make() -> T


class NccAncestorMiddle:
	pass


class NccAncestorHolder extends NccAncestorMiddle:
	pass


extend NccAncestorMiddle uses NccAncestorKeeper[int]:
	func make() -> int:
		return 7


extend NccAncestorHolder uses NccAncestorKeeper[String]:
	func make() -> String:
		return "seven"


func test() -> void:
	pass
)");

	CHECK(fixture.has_error_containing("is already applied with different type arguments"));
	CHECK(fixture.has_error_containing("NccAncestorKeeper"));
	CHECK(fixture.has_error_containing("NccAncestorMiddle"));
}

TEST_CASE("[Modules][FoundryScript][Conformance] The script-ancestor rule does not depend on declaration order") {
	NativeChainFixture fixture(R"(
trait NccAncestorOrderKeeper[T]:
	abstract func make() -> T


class NccAncestorOrderMiddle:
	pass


class NccAncestorOrderHolder extends NccAncestorOrderMiddle:
	pass


extend NccAncestorOrderHolder uses NccAncestorOrderKeeper[String]:
	func make() -> String:
		return "seven"


extend NccAncestorOrderMiddle uses NccAncestorOrderKeeper[int]:
	func make() -> int:
		return 7


func test() -> void:
	pass
)");

	CHECK(fixture.has_error_containing("is already applied with different type arguments"));
	CHECK(fixture.has_error_containing("NccAncestorOrderKeeper"));
	CHECK(fixture.has_error_containing("NccAncestorOrderHolder"));
}

TEST_CASE("[Modules][FoundryScript][Conformance] A descendant's uses clause conflicts with a script ancestor's conformance") {
	NativeChainFixture fixture(R"(
trait NccAncestorUsesKeeper[T]:
	abstract func make() -> T


class NccAncestorUsesMiddle:
	pass


class NccAncestorUsesHolder extends NccAncestorUsesMiddle uses NccAncestorUsesKeeper[String]:
	func make() -> String:
		return "seven"


extend NccAncestorUsesMiddle uses NccAncestorUsesKeeper[int]:
	func make() -> int:
		return 7


func test() -> void:
	pass
)");

	CHECK(fixture.has_error_containing("is already applied with different type arguments"));
	CHECK(fixture.has_error_containing("NccAncestorUsesKeeper"));
}

TEST_CASE("[Modules][FoundryScript][Conformance] A rejected script-ancestor conformance registers nothing") {
	NativeChainFixture fixture(R"(
trait NccAncestorRejectedKeeper[T]:
	abstract func make() -> T


class NccAncestorRejectedMiddle:
	pass


class NccAncestorRejectedHolder extends NccAncestorRejectedMiddle:
	pass


extend NccAncestorRejectedMiddle uses NccAncestorRejectedKeeper[int]:
	func make() -> int:
		return 7


extend NccAncestorRejectedHolder uses NccAncestorRejectedKeeper[String]:
	func make() -> String:
		return "seven"


func test() -> void:
	pass
)");
	REQUIRE(fixture.has_error_containing("is already applied with different type arguments"));

	// The ancestor's declaration is coherent on its own and stays; the contradicting descendant publishes
	// neither a membership nor a witness, so nothing dispatches through a conformance the program was
	// rejected over.
	const StringName identity = fixture.trait_identity(StringName("NccAncestorRejectedKeeper"));
	const Vector<FSConformanceRegistry::ScriptConformanceRecord> records =
			FSConformanceRegistry::get_singleton()->get_script_conformance_records(identity);
	REQUIRE_EQ(records.size(), 1);
	CHECK(records[0].target_label.contains("NccAncestorRejectedMiddle"));
	String witness_source;
	int witness_conformance_index = -1;
	CHECK_FALSE(FSConformanceRegistry::get_singleton()->find_witness_location(
			fixture.class_fqcn(StringName("NccAncestorRejectedHolder")), StringName("make"), witness_source,
			witness_conformance_index));
}

TEST_CASE("[Modules][FoundryScript][Conformance] A script class may repeat its script ancestor's arguments") {
	NativeChainFixture fixture(R"(
trait NccAncestorAgreeKeeper[T]:
	abstract func make() -> T


class NccAncestorAgreeMiddle:
	pass


class NccAncestorAgreeHolder extends NccAncestorAgreeMiddle:
	pass


extend NccAncestorAgreeMiddle uses NccAncestorAgreeKeeper[int]:
	func make() -> int:
		return 7


extend NccAncestorAgreeHolder uses NccAncestorAgreeKeeper[int]:
	func make() -> int:
		return 11


func test() -> void:
	pass
)");

	CHECK(fixture.error_messages.is_empty());
}

TEST_CASE("[Modules][FoundryScript][Conformance] Script classes on different branches may bind a trait differently") {
	NativeChainFixture fixture(R"(
trait NccAncestorBranchKeeper[T]:
	abstract func make() -> T


class NccAncestorBranchMiddle:
	pass


class NccAncestorBranchSibling:
	pass


extend NccAncestorBranchMiddle uses NccAncestorBranchKeeper[int]:
	func make() -> int:
		return 7


extend NccAncestorBranchSibling uses NccAncestorBranchKeeper[String]:
	func make() -> String:
		return "seven"


func test() -> void:
	pass
)");

	// Neither class inherits from the other, so no value ever reaches both conformances.
	CHECK(fixture.error_messages.is_empty());
}

TEST_CASE("[Modules][FoundryScript][Conformance] An implied supertrait identity is checked across a script ancestry") {
	NativeChainFixture fixture(R"(
trait NccAncestorBaseKeeper[T]:
	abstract func make() -> T


trait NccAncestorDerivedKeeper[T] uses NccAncestorBaseKeeper[T]:
	abstract func label() -> String


class NccAncestorImpliedMiddle:
	pass


class NccAncestorImpliedHolder extends NccAncestorImpliedMiddle:
	pass


extend NccAncestorImpliedMiddle uses NccAncestorBaseKeeper[int]:
	func make() -> int:
		return 7


extend NccAncestorImpliedHolder uses NccAncestorDerivedKeeper[String]:
	func make() -> String:
		return "seven"

	func label() -> String:
		return "holder"


func test() -> void:
	pass
)");

	// The conflict is on the implied `NccAncestorBaseKeeper` identity, not on the trait the declaration
	// names.
	CHECK(fixture.has_error_containing("is already applied with different type arguments"));
	CHECK(fixture.has_error_containing("NccAncestorBaseKeeper"));
}

TEST_CASE("[Modules][FoundryScript][Conformance] A final descendant's reified Self conflicts with its ancestor's binding") {
	NativeChainFixture fixture(R"(
trait NccSelfKeeper[T]:
	func label() -> String:
		return "keeper"


class NccSelfPair[A, B]:
	pass


class NccSelfBase:
	uses NccSelfKeeper[NccSelfPair[int, String]]


final class NccSelfSub extends NccSelfBase:
	uses NccSelfKeeper[NccSelfPair[int, Self]]


func test() -> void:
	pass
)");

	// The applied vector is reified before the chain comparison, so the descendant's `Self` is read as
	// `NccSelfSub` and contradicts the `String` the ancestor fixed. The diagnostic renders the reified
	// form rather than the `Self` the author typed.
	CHECK(fixture.has_error_containing(R"(cannot re-apply it with ("NccSelfPair[int, NccSelfSub]"))"));
	CHECK(fixture.has_error_containing(R"(already applied with type arguments ("NccSelfPair[int, String]") by "NccSelfBase")"));
}

TEST_CASE("[Modules][FoundryScript][Conformance] The reified-Self chain rule does not depend on declaration order") {
	NativeChainFixture fixture(R"(
trait NccSelfOrderKeeper[T]:
	func label() -> String:
		return "keeper"


final class NccSelfOrderSub extends NccSelfOrderBase:
	uses NccSelfOrderKeeper[NccSelfOrderPair[int, Self]]


class NccSelfOrderBase:
	uses NccSelfOrderKeeper[NccSelfOrderPair[int, String]]


class NccSelfOrderPair[A, B]:
	pass


func test() -> void:
	pass
)");

	CHECK(fixture.has_error_containing(R"(cannot re-apply it with ("NccSelfOrderPair[int, NccSelfOrderSub]"))"));
}

TEST_CASE("[Modules][FoundryScript][Conformance] A retroactive conformance reifies Self against its target") {
	NativeChainFixture fixture(R"(
trait NccSelfRetroKeeper[T]:
	func label() -> String:
		return "keeper"


class NccSelfRetroPair[A, B]:
	pass


class NccSelfRetroBase:
	uses NccSelfRetroKeeper[NccSelfRetroPair[int, String]]


final class NccSelfRetroSub extends NccSelfRetroBase:
	pass


extend NccSelfRetroSub uses NccSelfRetroKeeper[NccSelfRetroPair[int, Self]]:
	pass


func test() -> void:
	pass
)");

	// The conformance target, not the declaring file's head class, is what `Self` names here.
	CHECK(fixture.has_error_containing(R"(cannot record ("NccSelfRetroPair[int, NccSelfRetroSub]"))"));
}

TEST_CASE("[Modules][FoundryScript][Conformance] A rejected reified-Self conformance registers nothing") {
	NativeChainFixture fixture(R"(
trait NccSelfRejectedKeeper[T]:
	func label() -> String:
		return "keeper"


class NccSelfRejectedPair[A, B]:
	pass


class NccSelfRejectedBase:
	uses NccSelfRejectedKeeper[NccSelfRejectedPair[int, String]]


final class NccSelfRejectedSub extends NccSelfRejectedBase:
	pass


extend NccSelfRejectedSub uses NccSelfRejectedKeeper[NccSelfRejectedPair[int, Self]]:
	pass


func test() -> void:
	pass
)");

	REQUIRE(fixture.has_error_containing("cannot record"));

	// A rejected declaration publishes neither membership nor argument evidence, so the registry holds
	// nothing at all for the identity: the file's only conformance is the rejected one.
	const StringName identity = fixture.trait_identity(StringName("NccSelfRejectedKeeper"));
	CHECK(FSConformanceRegistry::get_singleton()->get_script_conformance_records(identity).is_empty());
}

TEST_CASE("[Modules][FoundryScript][Conformance] An implied supertrait identity is checked after Self reification") {
	NativeChainFixture fixture(R"(
trait NccSelfSuperKeeper[T]:
	func label() -> String:
		return "keeper"


trait NccSelfSuperStoring[T] uses NccSelfSuperKeeper[T]:
	func store_label() -> String:
		return "storing"


class NccSelfSuperPair[A, B]:
	pass


class NccSelfSuperBase:
	uses NccSelfSuperKeeper[NccSelfSuperPair[int, String]]


final class NccSelfSuperSub extends NccSelfSuperBase:
	uses NccSelfSuperStoring[NccSelfSuperPair[int, Self]]


func test() -> void:
	pass
)");

	// The conflict is on the implied identity, whose projected argument carries the same reified `Self`.
	CHECK(fixture.has_error_containing("NccSelfSuperKeeper"));
	CHECK(fixture.has_error_containing(R"(cannot re-apply it with ("NccSelfSuperPair[int, NccSelfSuperSub]"))"));
}

TEST_CASE("[Modules][FoundryScript][Conformance] An ancestor that names the descendant matches its reified Self") {
	NativeChainFixture fixture(R"(
trait NccSelfMatchKeeper[T]:
	func label() -> String:
		return "keeper"


class NccSelfMatchPair[A, B]:
	pass


class NccSelfMatchBase:
	uses NccSelfMatchKeeper[NccSelfMatchPair[int, NccSelfMatchSub]]


final class NccSelfMatchSub extends NccSelfMatchBase:
	uses NccSelfMatchKeeper[NccSelfMatchPair[int, Self]]


class NccSelfMatchRetroBase:
	uses NccSelfMatchKeeper[NccSelfMatchPair[int, NccSelfMatchRetroSub]]


final class NccSelfMatchRetroSub extends NccSelfMatchRetroBase:
	pass


extend NccSelfMatchRetroSub uses NccSelfMatchKeeper[NccSelfMatchPair[int, Self]]:
	pass


func test() -> void:
	pass
)");

	// Reification cuts both ways: an ancestor that spells the descendant out states exactly what the
	// descendant's `Self` states, so neither form is a conflict.
	CHECK(fixture.error_messages.is_empty());
}

TEST_CASE("[Modules][FoundryScript][Conformance] A non-final implementer's Self stays open against a chain binding") {
	NativeChainFixture fixture(R"(
trait NccSelfOpenKeeper[T]:
	func label() -> String:
		return "keeper"


class NccSelfOpenPair[A, B]:
	pass


class NccSelfOpenBase:
	uses NccSelfOpenKeeper[NccSelfOpenPair[int, String]]


class NccSelfOpenSub extends NccSelfOpenBase:
	uses NccSelfOpenKeeper[NccSelfOpenPair[int, Self]]


func test() -> void:
	pass
)");

	// `NccSelfOpenSub` is not final, so `Self` denotes no single class and the position states nothing
	// that could contradict the ancestor. The concrete `int` sibling still agrees.
	CHECK(fixture.error_messages.is_empty());
}

} // namespace FSNativeChainCoherenceTests
