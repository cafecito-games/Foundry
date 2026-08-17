# A specialized return type asks about the returned value's reified arguments even when the returned
# expression is statically typed. A static type proves the whole contract only when it is exact; an
# unspecialized base, a subclass whose arguments are known through its ancestor bindings, or a trait
# conformer proves the nominal half alone, so the answer is left to the runtime. The rule there is the
# gradual one, so exact, projected and absent evidence are all accepted.
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


func give_exact() -> Pair[int, String]:
	var value: Pair[int, String] = supply(Pair[int, String].new())
	return value


func give_unspecialized_source() -> Pair[int, String]:
	var value: Pair = supply(Pair.new())
	return value


func give_projected_subclass() -> Pair[int, String]:
	var value: IntStringPair = IntStringPair.new()
	return value


func give_forwarded_subclass() -> Pair[int, String]:
	var value: ForwardingPair[String] = ForwardingPair[String].new()
	return value


func give_null() -> Pair[int, String]:
	var value: Pair = supply(null)
	return value


func give_declared_conformer() -> Holder[int]:
	var value: IntHolder = IntHolder.new()
	return value


func give_inherited_conformer() -> Holder[int]:
	var value: DerivedIntHolder = DerivedIntHolder.new()
	return value


func give_supertrait_conformer() -> Holder[int]:
	var value: StoringHolder = StoringHolder.new()
	return value


func give_forwarded_conformer() -> Holder[int]:
	var value: ForwardingHolder[int] = ForwardingHolder[int].new()
	return value


func give_absent_trait_evidence() -> Holder[int]:
	var value: Holder = supply(ForwardingHolder.new())
	return value


func give_retroactive_conformer() -> Holder[int]:
	var value: RetroHolderTarget = RetroHolderTarget.new()
	return value


func test() -> void:
	print("exact: ", give_exact() != null)
	print("unspecialized source: ", give_unspecialized_source() != null)
	print("projected subclass: ", give_projected_subclass() != null)
	print("forwarded subclass: ", give_forwarded_subclass() != null)
	print("null: ", give_null() == null)
	print("declared conformance: ", give_declared_conformer() != null)
	print("inherited conformance: ", give_inherited_conformer() != null)
	print("supertrait conformance: ", give_supertrait_conformer() != null)
	print("forwarded conformance: ", give_forwarded_conformer() != null)
	print("absent trait evidence: ", give_absent_trait_evidence() != null)
	print("retroactive conformance: ", give_retroactive_conformer() != null)
