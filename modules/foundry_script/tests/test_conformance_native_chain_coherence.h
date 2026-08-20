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

TEST_CASE("[Modules][FoundryScript][Conformance] A conformance that supplies no trait arguments is rejected") {
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

	// The bare conformance is an arity error and is dropped, so it contributes no evidence: the
	// specialized conformance on the subclass registers without a chain-coherence conflict.
	CHECK_EQ(fixture.error_messages.size(), 1);
	CHECK(fixture.has_error_containing(R"(Generic trait "NccOpenKeeper" expects 1 type argument(s), but 0 were given.)"));
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
	REQUIRE_OR_RETURN(records.size() == 1);
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

TEST_CASE("[Modules][FoundryScript][Conformance] A native conformance without arguments is rejected") {
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

	// The bare native conformance is an arity error and is dropped, so the script class's
	// specialized conformance registers without a chain-coherence conflict against it.
	CHECK_EQ(fixture.error_messages.size(), 1);
	CHECK(fixture.has_error_containing(R"(Generic trait "NccOpenScriptKeeper" expects 1 type argument(s), but 0 were given.)"));
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
	REQUIRE_OR_RETURN(records.size() == 1);
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

// A class that applies a generic trait through its own `uses` clause registers no conformance: it
// declares no external membership and supplies no witnesses. It still fixes that trait's arguments for
// every receiver on its chain, so a declaration in another file that binds the same identity
// differently is the same contradiction two conformances would be — and the only file that can see
// both sides is the one that loads the other.
//
// These cases drive the registry directly, because the asymmetry they are about is a property of what
// one file may know about another rather than of any single parse tree.
namespace TraitBindingRecords {

// Whatever a case registers is dropped again: the registry is a process-global singleton.
class BindingScope {
	Vector<String> source_files;

public:
	void track(const String &p_source_file) {
		if (!source_files.has(p_source_file)) {
			source_files.push_back(p_source_file);
		}
	}

	~BindingScope() {
		for (const String &source_file : source_files) {
			FSConformanceRegistry::get_singleton()->clear_file(source_file);
		}
	}
};

static Vector<FSConformanceRegistry::RecordedTypeArgument> builtin_arguments(Variant::Type p_type) {
	FSConformanceRegistry::RecordedTypeArgument argument;
	argument.kind = FSConformanceRegistry::RecordedTypeArgument::BUILTIN;
	argument.builtin_type = p_type;
	Vector<FSConformanceRegistry::RecordedTypeArgument> arguments;
	arguments.push_back(argument);
	return arguments;
}

// One class's `uses` binding, as its declaring file publishes it: the class belongs to a script file,
// and its chain bottoms out at `p_native_base`.
static Vector<FSConformanceRegistry::ClassTraitBinding> class_binding(const String &p_source_file,
		const String &p_target_fqcn, const StringName &p_native_base, const StringName &p_trait_name,
		Variant::Type p_argument, const Vector<String> &p_script_ancestors = Vector<String>()) {
	FSConformanceRegistry::ClassTraitBinding binding;
	binding.target_fqcn = p_target_fqcn;
	binding.target_label = p_target_fqcn.get_file();
	binding.target_native_base = p_native_base;
	binding.target_script_ancestor_fqcns = p_script_ancestors;
	binding.trait_name = p_trait_name;
	binding.trait_type_arguments = builtin_arguments(p_argument);
	binding.source_file = p_source_file;
	Vector<FSConformanceRegistry::ClassTraitBinding> bindings;
	bindings.push_back(binding);
	return bindings;
}

static Vector<FSConformanceRegistry::Conformance> native_conformance(const String &p_source_file,
		const StringName &p_native_class, const StringName &p_trait_name, Variant::Type p_argument) {
	FSConformanceRegistry::Conformance conformance;
	conformance.target_keys.push_back(String(p_native_class));
	conformance.target_fqcn = String(p_native_class);
	conformance.target_label = String(p_native_class);
	conformance.trait_name = p_trait_name;
	conformance.trait_type_arguments = builtin_arguments(p_argument);
	conformance.source_file = p_source_file;
	conformance.conformance_index = 0;
	Vector<FSConformanceRegistry::Conformance> candidates;
	candidates.push_back(conformance);
	return candidates;
}

static Vector<FSConformanceRegistry::Conformance> script_conformance(const String &p_source_file,
		const String &p_target_fqcn, const String &p_target_script_path, const StringName &p_native_base,
		const StringName &p_trait_name, Variant::Type p_argument) {
	FSConformanceRegistry::Conformance conformance;
	conformance.target_keys.push_back(p_target_fqcn);
	conformance.target_fqcn = p_target_fqcn;
	conformance.target_script_path = p_target_script_path;
	conformance.target_label = p_target_fqcn.get_file();
	conformance.target_native_base = p_native_base;
	conformance.trait_name = p_trait_name;
	conformance.trait_type_arguments = builtin_arguments(p_argument);
	conformance.source_file = p_source_file;
	conformance.conformance_index = 0;
	Vector<FSConformanceRegistry::Conformance> candidates;
	candidates.push_back(conformance);
	return candidates;
}

TEST_CASE("[Modules][FoundryScript][Conformance] Engine declaration rejects a loaded dependency's class-uses binding") {
	FSConformanceRegistry *registry = FSConformanceRegistry::get_singleton();
	BindingScope scope;
	const String dependency_file = "user://ncc_binding_dependency.fs";
	const String declaring_file = "user://ncc_binding_declaration.fs";
	const StringName trait_name = "NccBindingKeeper";
	scope.track(dependency_file);
	scope.track(declaring_file);

	registry->try_replace_file_conformances(dependency_file, Vector<FSConformanceRegistry::Conformance>(),
			class_binding(dependency_file, dependency_file + "::Holder", "RefCounted", trait_name, Variant::STRING));

	const FSConformanceRegistry::RegistrationResult result = registry->try_replace_file_conformances(
			declaring_file, native_conformance(declaring_file, "RefCounted", trait_name, Variant::INT));

	REQUIRE_OR_RETURN(result.conflicts.size() == 1);
	CHECK_EQ(result.conflicts[0].kind, FSConformanceRegistry::RegistrationConflict::CHAIN_COHERENCE);
	CHECK_EQ(result.conflicts[0].conflicting_source_file, dependency_file);
	CHECK_EQ(result.conflicts[0].trait_name, trait_name);
	CHECK_EQ(result.registered_count, 0);
}

TEST_CASE("[Modules][FoundryScript][Conformance] Dependency class-uses binding registered before the engine declaration still rejects") {
	FSConformanceRegistry *registry = FSConformanceRegistry::get_singleton();
	BindingScope scope;
	const String dependency_file = "user://ncc_binding_order_dependency.fs";
	const String declaring_file = "user://ncc_binding_order_declaration.fs";
	const StringName trait_name = "NccBindingOrderKeeper";
	scope.track(dependency_file);
	scope.track(declaring_file);

	// The declaration is analyzed first, while the dependency's binding is not published yet, so nothing
	// contradicts it. Publishing the binding and re-analyzing the declaring file — which is what loading
	// the dependency amounts to — must reach the same verdict as the other order.
	REQUIRE_EQ(registry->try_replace_file_conformances(declaring_file,
							   native_conformance(declaring_file, "RefCounted", trait_name, Variant::INT))
					   .registered_count,
			1);

	registry->try_replace_file_conformances(dependency_file, Vector<FSConformanceRegistry::Conformance>(),
			class_binding(dependency_file, dependency_file + "::Holder", "RefCounted", trait_name, Variant::STRING));

	const FSConformanceRegistry::RegistrationResult result = registry->try_replace_file_conformances(
			declaring_file, native_conformance(declaring_file, "RefCounted", trait_name, Variant::INT));

	REQUIRE_OR_RETURN(result.conflicts.size() == 1);
	CHECK_EQ(result.conflicts[0].kind, FSConformanceRegistry::RegistrationConflict::CHAIN_COHERENCE);
	CHECK_EQ(result.registered_count, 0);
}

TEST_CASE("[Modules][FoundryScript][Conformance] Matching class-uses binding accepted") {
	FSConformanceRegistry *registry = FSConformanceRegistry::get_singleton();
	BindingScope scope;
	const String dependency_file = "user://ncc_binding_match_dependency.fs";
	const String declaring_file = "user://ncc_binding_match_declaration.fs";
	const StringName trait_name = "NccBindingMatchKeeper";
	scope.track(dependency_file);
	scope.track(declaring_file);

	registry->try_replace_file_conformances(dependency_file, Vector<FSConformanceRegistry::Conformance>(),
			class_binding(dependency_file, dependency_file + "::Holder", "RefCounted", trait_name, Variant::INT));

	const FSConformanceRegistry::RegistrationResult result = registry->try_replace_file_conformances(
			declaring_file, native_conformance(declaring_file, "RefCounted", trait_name, Variant::INT));

	CHECK(result.conflicts.is_empty());
	CHECK_EQ(result.registered_count, 1);
}

TEST_CASE("[Modules][FoundryScript][Conformance] Binding records are dropped when the declaring file re-registers empty") {
	FSConformanceRegistry *registry = FSConformanceRegistry::get_singleton();
	BindingScope scope;
	const String dependency_file = "user://ncc_binding_drop_dependency.fs";
	const String declaring_file = "user://ncc_binding_drop_declaration.fs";
	const StringName trait_name = "NccBindingDropKeeper";
	scope.track(dependency_file);
	scope.track(declaring_file);

	registry->try_replace_file_conformances(dependency_file, Vector<FSConformanceRegistry::Conformance>(),
			class_binding(dependency_file, dependency_file + "::Holder", "RefCounted", trait_name, Variant::STRING));
	REQUIRE_EQ(registry->get_script_trait_binding_records(trait_name).size(), 1);

	// The dependency no longer binds anything, so what it used to bind must stop deciding other files.
	registry->try_replace_file_conformances(dependency_file, Vector<FSConformanceRegistry::Conformance>());
	CHECK(registry->get_script_trait_binding_records(trait_name).is_empty());

	const FSConformanceRegistry::RegistrationResult result = registry->try_replace_file_conformances(
			declaring_file, native_conformance(declaring_file, "RefCounted", trait_name, Variant::INT));

	CHECK(result.conflicts.is_empty());
	CHECK_EQ(result.registered_count, 1);
}

TEST_CASE("[Modules][FoundryScript][Conformance] Script-class conformance rejects a loaded dependency descendant's class-uses binding") {
	FSConformanceRegistry *registry = FSConformanceRegistry::get_singleton();
	BindingScope scope;
	const String base_file = "user://ncc_binding_script_base.fs";
	const String dependency_file = "user://ncc_binding_script_dependency.fs";
	const String declaring_file = "user://ncc_binding_script_declaration.fs";
	const String base_fqcn = "NccBindingScriptBase";
	const StringName trait_name = "NccBindingScriptKeeper";
	scope.track(dependency_file);
	scope.track(declaring_file);

	Vector<String> ancestors;
	ancestors.push_back(base_fqcn);
	registry->try_replace_file_conformances(dependency_file, Vector<FSConformanceRegistry::Conformance>(),
			class_binding(dependency_file, dependency_file + "::Holder", "RefCounted", trait_name, Variant::STRING, ancestors));

	const FSConformanceRegistry::RegistrationResult result = registry->try_replace_file_conformances(declaring_file,
			script_conformance(declaring_file, base_fqcn, base_file, "RefCounted", trait_name, Variant::INT));

	REQUIRE_OR_RETURN(result.conflicts.size() == 1);
	CHECK_EQ(result.conflicts[0].kind, FSConformanceRegistry::RegistrationConflict::CHAIN_COHERENCE);
	CHECK_EQ(result.conflicts[0].conflicting_source_file, dependency_file);
	CHECK_EQ(result.registered_count, 0);
}

TEST_CASE("[Modules][FoundryScript][Conformance] Conformance published first still contradicts a later binding") {
	FSConformanceRegistry *registry = FSConformanceRegistry::get_singleton();
	BindingScope scope;
	const String dependency_file = "user://ncc_binding_mirror_dependency.fs";
	const String declaring_file = "user://ncc_binding_mirror_declaration.fs";
	const StringName trait_name = "NccBindingMirrorKeeper";
	scope.track(dependency_file);
	scope.track(declaring_file);

	// The mirror of the case above. Analyses run concurrently and each publishes under the registry's
	// own lock, so the conformance may reach it first; the file publishing the binding is then the only
	// one that can see both sides, and the contradiction has to be found as its bindings are stored.
	REQUIRE_EQ(registry->try_replace_file_conformances(declaring_file,
							   native_conformance(declaring_file, "RefCounted", trait_name, Variant::INT))
					   .registered_count,
			1);

	const FSConformanceRegistry::RegistrationResult result = registry->try_replace_file_conformances(
			dependency_file, Vector<FSConformanceRegistry::Conformance>(),
			class_binding(dependency_file, dependency_file + "::Holder", "RefCounted", trait_name, Variant::STRING));

	REQUIRE_OR_RETURN(result.binding_conflicts.size() == 1);
	CHECK_EQ(result.binding_conflicts[0].target_fqcn, dependency_file + "::Holder");
	CHECK_EQ(result.binding_conflicts[0].trait_name, trait_name);
	CHECK_EQ(result.binding_conflicts[0].conflicting_source_file, declaring_file);

	// Reported, never arbitrated: the binding is still stored, so the chain it fixes stays recorded and a
	// third file asking about it is not answered with silence.
	CHECK_EQ(registry->get_script_trait_binding_records(trait_name).size(), 1);
}

TEST_CASE("[Modules][FoundryScript][Conformance] A matching binding published after a conformance reports nothing") {
	FSConformanceRegistry *registry = FSConformanceRegistry::get_singleton();
	BindingScope scope;
	const String dependency_file = "user://ncc_binding_mirror_ok_dependency.fs";
	const String declaring_file = "user://ncc_binding_mirror_ok_declaration.fs";
	const StringName trait_name = "NccBindingMirrorOkKeeper";
	scope.track(dependency_file);
	scope.track(declaring_file);

	registry->try_replace_file_conformances(declaring_file,
			native_conformance(declaring_file, "RefCounted", trait_name, Variant::INT));

	const FSConformanceRegistry::RegistrationResult result = registry->try_replace_file_conformances(
			dependency_file, Vector<FSConformanceRegistry::Conformance>(),
			class_binding(dependency_file, dependency_file + "::Holder", "RefCounted", trait_name, Variant::INT));

	CHECK(result.binding_conflicts.is_empty());
}

// A file's own view of the registry: it sees itself and whatever it loads, and nothing else. The real
// analyzer installs the same shape, which is what makes the visibility relation directional.
class LoadsOnlyVisibility : public FSConformanceRegistry::Visibility {
	HashSet<String> visible;

public:
	explicit LoadsOnlyVisibility(const Vector<String> &p_visible) {
		for (const String &file : p_visible) {
			visible.insert(file);
		}
	}

	bool can_see(const String &p_source_file) const override {
		return visible.has(p_source_file);
	}
};

TEST_CASE("[Modules][FoundryScript][Conformance] A loaded file's binding contradicts the loader's conformance published first") {
	FSConformanceRegistry *registry = FSConformanceRegistry::get_singleton();
	BindingScope scope;
	// The loader declares the conformance; the file it loads declares the binding. The edge runs one way
	// only, which is what makes the second publisher unable to see the first.
	const String loader_file = "user://ncc_binding_edge_loader.fs";
	const String loaded_file = "user://ncc_binding_edge_loaded.fs";
	const StringName trait_name = "NccBindingEdgeKeeper";
	scope.track(loader_file);
	scope.track(loaded_file);

	Vector<String> loader_sees;
	loader_sees.push_back(loader_file);
	loader_sees.push_back(loaded_file);
	HashSet<String> loader_loads;
	loader_loads.insert(loaded_file);

	{
		// The loader publishes first, while the loaded file's binding does not exist yet, so it has
		// nothing to find. This is the ordering a concurrent analysis of the two files produces when the
		// loader's dependency raising returns early on a parser another thread is still working through.
		const LoadsOnlyVisibility loader_visibility(loader_sees);
		const FSConformanceRegistry::ScopedVisibility scoped(&loader_visibility);
		REQUIRE_EQ(registry->try_replace_file_conformances(loader_file,
								   native_conformance(loader_file, "RefCounted", trait_name, Variant::INT),
								   Vector<FSConformanceRegistry::ClassTraitBinding>(), loader_loads)
						   .registered_count,
				1);
	}

	Vector<String> loaded_sees;
	loaded_sees.push_back(loaded_file);
	const LoadsOnlyVisibility loaded_visibility(loaded_sees);
	const FSConformanceRegistry::ScopedVisibility scoped(&loaded_visibility);

	// The loaded file cannot see the loader at all. The comparison is licensed by the loader's own load
	// edge, recorded when it published, so the contradiction is still found.
	CHECK_FALSE(registry->has_conformance("RefCounted", trait_name));

	const FSConformanceRegistry::RegistrationResult result = registry->try_replace_file_conformances(
			loaded_file, Vector<FSConformanceRegistry::Conformance>(),
			class_binding(loaded_file, loaded_file + "::Holder", "RefCounted", trait_name, Variant::STRING));

	REQUIRE_OR_RETURN(result.binding_conflicts.size() == 1);
	CHECK_EQ(result.binding_conflicts[0].conflicting_source_file, loader_file);
	CHECK_EQ(result.binding_conflicts[0].trait_name, trait_name);
}

TEST_CASE("[Modules][FoundryScript][Conformance] A loader's binding contradicts a loaded file's conformance published second") {
	FSConformanceRegistry *registry = FSConformanceRegistry::get_singleton();
	BindingScope scope;
	// The reverse of the case above: the file that loads is the one declaring only `uses`, and the file
	// it loads declares the contradicting conformance. The edge still runs one way only, so the second
	// publisher again cannot see the first.
	const String loader_file = "user://ncc_binding_edge_binding_loader.fs";
	const String loaded_file = "user://ncc_binding_edge_conformance_loaded.fs";
	const StringName trait_name = "NccBindingEdgeLoaderKeeper";
	scope.track(loader_file);
	scope.track(loaded_file);

	Vector<String> loader_sees;
	loader_sees.push_back(loader_file);
	loader_sees.push_back(loaded_file);
	HashSet<String> loader_loads;
	loader_loads.insert(loaded_file);

	{
		// The loader publishes its binding first, while the loaded file's conformance does not exist yet,
		// so it has nothing to find. Its load closure is published with the binding, which is the only
		// record of the edge either side will ever have.
		const LoadsOnlyVisibility loader_visibility(loader_sees);
		const FSConformanceRegistry::ScopedVisibility scoped(&loader_visibility);
		const FSConformanceRegistry::RegistrationResult loader_result = registry->try_replace_file_conformances(
				loader_file, Vector<FSConformanceRegistry::Conformance>(),
				class_binding(loader_file, loader_file + "::Holder", "RefCounted", trait_name, Variant::STRING),
				loader_loads);
		CHECK(loader_result.binding_conflicts.is_empty());
	}

	Vector<String> loaded_sees;
	loaded_sees.push_back(loaded_file);
	const LoadsOnlyVisibility loaded_visibility(loaded_sees);
	const FSConformanceRegistry::ScopedVisibility scoped(&loaded_visibility);

	// The loaded file cannot see the loader at all. The comparison is licensed by the loader's own load
	// edge, recorded when its binding published, so the contradiction is still found.
	const FSConformanceRegistry::RegistrationResult result = registry->try_replace_file_conformances(
			loaded_file, native_conformance(loaded_file, "RefCounted", trait_name, Variant::INT));

	REQUIRE_OR_RETURN(result.conflicts.size() == 1);
	CHECK_EQ(result.conflicts[0].kind, FSConformanceRegistry::RegistrationConflict::CHAIN_COHERENCE);
	CHECK_EQ(result.conflicts[0].conflicting_source_file, loader_file);
	CHECK_EQ(result.conflicts[0].trait_name, trait_name);
	CHECK_EQ(result.registered_count, 0);
}

TEST_CASE("[Modules][FoundryScript][Conformance] A conformance with no load edge either way stays uncompared") {
	FSConformanceRegistry *registry = FSConformanceRegistry::get_singleton();
	BindingScope scope;
	const String unrelated_file = "user://ncc_binding_noedge_unrelated.fs";
	const String binding_file = "user://ncc_binding_noedge_binding.fs";
	const String other_file = "user://ncc_binding_noedge_other.fs";
	const StringName trait_name = "NccBindingNoEdgeKeeper";
	scope.track(unrelated_file);
	scope.track(binding_file);

	Vector<String> binding_sees;
	binding_sees.push_back(binding_file);
	Vector<String> unrelated_sees;
	unrelated_sees.push_back(unrelated_file);

	{
		// The conformance's file loads something, but not the file that binds the trait.
		HashSet<String> unrelated_loads;
		unrelated_loads.insert(other_file);
		registry->try_replace_file_conformances(unrelated_file,
				native_conformance(unrelated_file, "RefCounted", trait_name, Variant::INT),
				Vector<FSConformanceRegistry::ClassTraitBinding>(), unrelated_loads);
	}

	{
		const LoadsOnlyVisibility binding_visibility(binding_sees);
		const FSConformanceRegistry::ScopedVisibility scoped(&binding_visibility);

		const FSConformanceRegistry::RegistrationResult result = registry->try_replace_file_conformances(
				binding_file, Vector<FSConformanceRegistry::Conformance>(),
				class_binding(binding_file, binding_file + "::Holder", "RefCounted", trait_name, Variant::STRING));

		// Neither file loads the other, so no single declaration site composes them. That incoherence
		// belongs to whatever file loads them both, and is deliberately not decided here.
		CHECK(result.binding_conflicts.is_empty());
	}

	// The same pair in the other publication order. Both sides now record their load closures, so the
	// verdict has to come from the absent edge rather than from which one happened to publish first.
	{
		HashSet<String> binding_loads;
		binding_loads.insert(other_file);
		const LoadsOnlyVisibility binding_visibility(binding_sees);
		const FSConformanceRegistry::ScopedVisibility scoped(&binding_visibility);
		const FSConformanceRegistry::RegistrationResult result = registry->try_replace_file_conformances(
				binding_file, Vector<FSConformanceRegistry::Conformance>(),
				class_binding(binding_file, binding_file + "::Holder", "RefCounted", trait_name, Variant::STRING),
				binding_loads);
		CHECK(result.binding_conflicts.is_empty());
	}

	{
		HashSet<String> unrelated_loads;
		unrelated_loads.insert(other_file);
		const LoadsOnlyVisibility unrelated_visibility(unrelated_sees);
		const FSConformanceRegistry::ScopedVisibility scoped(&unrelated_visibility);
		const FSConformanceRegistry::RegistrationResult result = registry->try_replace_file_conformances(
				unrelated_file, native_conformance(unrelated_file, "RefCounted", trait_name, Variant::INT),
				Vector<FSConformanceRegistry::ClassTraitBinding>(), unrelated_loads);

		CHECK(result.conflicts.is_empty());
		CHECK_EQ(result.registered_count, 1);
	}
}

TEST_CASE("[Modules][FoundryScript][Conformance] Binding records answer no membership or witness query") {
	FSConformanceRegistry *registry = FSConformanceRegistry::get_singleton();
	BindingScope scope;
	const String dependency_file = "user://ncc_binding_opaque_dependency.fs";
	const StringName trait_name = "NccBindingOpaqueKeeper";
	const String target_fqcn = dependency_file + "::Holder";
	scope.track(dependency_file);

	registry->try_replace_file_conformances(dependency_file, Vector<FSConformanceRegistry::Conformance>(),
			class_binding(dependency_file, target_fqcn, "RefCounted", trait_name, Variant::STRING));

	// A `uses` clause is the class's own membership, not an external conformance. The registry answers
	// only the coherence question about it; every query the type system and the runtime ask stays empty,
	// so nothing dispatches through a witness that was never declared.
	CHECK_FALSE(registry->has_conformance(target_fqcn, trait_name));
	CHECK_FALSE(registry->native_class_conforms("RefCounted", trait_name));
	CHECK(registry->get_conformance_source(target_fqcn, trait_name).is_empty());
	CHECK(registry->get_witnesses(target_fqcn, trait_name).is_empty());
	CHECK(registry->get_file_conformances(dependency_file).is_empty());
	CHECK(registry->get_script_conformance_records(trait_name).is_empty());
	Vector<FSConformanceRegistry::RecordedTypeArgument> arguments;
	CHECK_FALSE(registry->get_recorded_trait_arguments(target_fqcn, trait_name, arguments));
	CHECK(registry->find_witness_function(target_fqcn, "make") == nullptr);
}

} // namespace TraitBindingRecords

} // namespace FSNativeChainCoherenceTests
