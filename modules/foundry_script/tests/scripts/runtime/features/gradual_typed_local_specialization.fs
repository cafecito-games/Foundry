# A local whose declared type is specialized keeps those arguments when the value arrives through a
# gradual source: the store asks the same invariant question the member, call, tuple-element and return
# boundaries ask. The rule is the gradual one, so exact and projected evidence is accepted, and a value
# that states nothing about the declaration's parameters is accepted too.
#
# Every value here travels through a `Variant` source, which is what leaves the answer to the runtime
# instead of to the analyzer.
class Pair[A, B]:
	pass


class IntStringPair extends Pair[int, String]:
	pass


class ForwardingPair[B] extends Pair[int, B]:
	pass


trait Holder[T]:
	func label() -> String:
		return "holder"


trait Storing[T]:
	uses Holder[T]


class IntHolder:
	uses Holder[int]


class DerivedIntHolder extends IntHolder:
	pass


class StoringHolder:
	uses Storing[int]


class ForwardingHolder[U]:
	uses Holder[U]


class RetroHolderTarget:
	pass


extend RetroHolderTarget uses Holder[int]:
	func label() -> String:
		return "retroactive holder"


func supply(value: Variant) -> Variant:
	return value


func test() -> void:
	var exact: Pair[int, String] = supply(Pair[int, String].new())
	print("exact: ", exact != null)

	var raw: Pair[int, String] = supply(Pair.new())
	print("absent evidence: ", raw != null)

	var projected: Pair[int, String] = supply(IntStringPair.new())
	print("projected subclass: ", projected != null)

	var forwarded: Pair[int, String] = supply(ForwardingPair[String].new())
	print("forwarded subclass: ", forwarded != null)

	var unspecialized: Pair = supply(Pair[int, Node].new())
	print("unspecialized slot: ", unspecialized != null)

	var empty: Pair[int, String] = supply(null)
	print("null: ", empty == null)

	var declared: Holder[int] = supply(IntHolder.new())
	print("declared conformance: ", declared != null)

	var inherited: Holder[int] = supply(DerivedIntHolder.new())
	print("inherited conformance: ", inherited != null)

	var supertrait: Holder[int] = supply(StoringHolder.new())
	print("supertrait conformance: ", supertrait != null)

	var forwarded_conformance: Holder[int] = supply(ForwardingHolder[int].new())
	print("forwarded conformance: ", forwarded_conformance != null)

	var unknown_conformance: Holder[int] = supply(ForwardingHolder.new())
	print("absent trait evidence: ", unknown_conformance != null)

	var retroactive: Holder[int] = supply(RetroHolderTarget.new())
	print("retroactive conformance: ", retroactive != null)

	var unspecialized_trait: Holder = supply(IntHolder.new())
	print("unspecialized trait slot: ", unspecialized_trait != null)
