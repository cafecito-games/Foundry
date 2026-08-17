# A constant declared inside a generic trait names the trait's own type parameters, and the arguments
# the implementer applied are known by the time the flattened copy is folded. Substituting them first
# lets a concrete application fold the specialized handle it really means, recursively through a
# composite argument, instead of baking the parameter's erasure in as the definite specialization
# `Variant`. A constant whose arguments are not all concrete after substitution stays the bare
# unspecialized handle -- a constant pool has no receiver, and a specialized handle has no
# representation for a partially known argument vector, so one forwarded argument leaves the whole
# handle bare.
#
# An applied argument that names `Self` is receiver-dependent: constructing through the constant's
# name resolves `Self` against the receiver, so a subclass of the implementer builds its own
# specialization. The flattened constant is one slot in the implementer's pool shared by every
# subclass, so folding `Self` is only honest when no other receiver is possible -- that is, when the
# implementer is `final`. A final implementer resolves `Self` at every nesting depth and carries the
# full specialization; a non-final one leaves the whole handle bare at every nesting depth, carrying
# no evidence rather than the implementer's own specialization, which a subclass receiver
# contradicts. Folding at the access site instead, so that reading the constant projects `Self` onto
# the receiver it was read through, would make both spellings agree in every case; it is the option
# not taken, because a receiver-dependent constant is no longer a compile-time constant fold.
class Holder[T]:
	var value: T


class Pair[A, B]:
	var first: A
	var second: B


trait Aliasing[V]:
	const Aliased = Holder[V]


trait Pairing[A, B]:
	func local_handle() -> Variant:
		const Local = Pair[A, B]
		return Local


class Fixed:
	uses Aliasing[int]

	# The implementer's own method names the flattened constant without a receiver, which resolves and
	# folds through a different path than the qualified `Fixed.Aliased` form.
	func bare_handle() -> Variant:
		return Aliased


class Composite:
	uses Aliasing[Array[int]]


class InheritingFixed extends Fixed:
	pass


class SelfApplied:
	uses Aliasing[Self]


class SubApplied extends SelfApplied:
	pass


class NestedApplied:
	uses Aliasing[Array[Self]]


class SubNested extends NestedApplied:
	pass


final class FinalApplied:
	uses Aliasing[Self]


final class FinalNested:
	uses Aliasing[Array[Self]]


final class FinalDeepNested:
	uses Aliasing[Holder[Array[Self]]]


class ConcretePairing:
	uses Pairing[int, String]


class MixedPairing[U]:
	uses Pairing[int, U]


func test() -> void:
	var aliased: Variant = Fixed.Aliased.new()
	print(aliased is Holder[int])
	print(aliased is Holder[String])
	print(aliased is Holder[Variant])

	var composite: Variant = Composite.Aliased.new()
	print(composite is Holder[Array[int]])
	print(composite is Holder[Array[String]])
	print(composite is Holder[Variant])

	print(Fixed.new().bare_handle() == Holder[int])
	print(Fixed.new().bare_handle() == Holder)

	# A trait member stays reachable through inheritance, so a subclass of the implementer sees the same
	# reified handle its base flattened in.
	var inherited: Variant = InheritingFixed.Aliased.new()
	print(inherited is Holder[int])
	print(inherited is Holder[Variant])

	# Reading the constant as a value folds the same reified handle the implementer's own constant pool
	# holds, so constructing through the stored handle agrees with constructing through the name.
	var stored: Variant = Fixed.Aliased
	@warning_ignore("UNSAFE_METHOD_ACCESS")
	var from_stored: Variant = stored.new()
	print(from_stored is Holder[int])
	print(from_stored is Holder[Variant])

	# A non-final implementer applying the trait with bare `Self`: the name form stays receiver-dependent
	# and correct, while the stored handle is bare for the implementer and for its subclass alike.
	var self_applied: Variant = SelfApplied.Aliased.new()
	print(self_applied is Holder[SelfApplied])
	print(self_applied is Holder[Variant])
	var sub_applied: Variant = SubApplied.Aliased.new()
	print(sub_applied is Holder[SubApplied])
	print(sub_applied is Holder[SelfApplied])
	var self_stored: Variant = SelfApplied.Aliased
	print(self_stored == Holder)
	print(self_stored == Holder[SelfApplied])
	var sub_self_stored: Variant = SubApplied.Aliased
	print(sub_self_stored == Holder)
	print(sub_self_stored == Holder[SubApplied])

	# The same rule one nesting level down. Folding here would bake the implementer into the shared slot,
	# which a subclass receiver directly contradicts: the name form builds `Holder[Array[SubNested]]`.
	var nested_built: Variant = NestedApplied.Aliased.new()
	print(nested_built is Holder[Array[NestedApplied]])
	var sub_nested_built: Variant = SubNested.Aliased.new()
	print(sub_nested_built is Holder[Array[SubNested]])
	print(sub_nested_built is Holder[Array[NestedApplied]])
	var nested_stored: Variant = NestedApplied.Aliased
	print(nested_stored == Holder)
	print(nested_stored == Holder[Array[NestedApplied]])
	var sub_nested_stored: Variant = SubNested.Aliased
	print(sub_nested_stored == Holder)
	print(sub_nested_stored == Holder[Array[NestedApplied]])
	print(sub_nested_stored == Holder[Array[SubNested]])
	@warning_ignore("UNSAFE_METHOD_ACCESS")
	var from_sub_nested_stored: Variant = sub_nested_stored.new()
	print(from_sub_nested_stored is Holder)
	print(from_sub_nested_stored is Holder[Array[NestedApplied]])

	# A final implementer has exactly one possible receiver for `Self`, so both spellings agree -- bare,
	# nested in a container, and nested inside another specialization.
	var final_applied: Variant = FinalApplied.Aliased.new()
	print(final_applied is Holder[FinalApplied])
	var final_stored: Variant = FinalApplied.Aliased
	print(final_stored == Holder[FinalApplied])
	print(final_stored == Holder)

	var final_nested: Variant = FinalNested.Aliased.new()
	print(final_nested is Holder[Array[FinalNested]])
	var final_nested_stored: Variant = FinalNested.Aliased
	print(final_nested_stored == Holder[Array[FinalNested]])
	print(final_nested_stored == Holder)

	var final_deep: Variant = FinalDeepNested.Aliased.new()
	print(final_deep is Holder[Holder[Array[FinalDeepNested]]])
	var final_deep_stored: Variant = FinalDeepNested.Aliased
	print(final_deep_stored == Holder[Holder[Array[FinalDeepNested]]])
	print(final_deep_stored == Holder)

	print(ConcretePairing.new().local_handle() == Pair[int, String])
	print(ConcretePairing.new().local_handle() == Pair)
	print(MixedPairing[String].new().local_handle() == Pair)
	print(MixedPairing[String].new().local_handle() == Pair[int, String])
	print("trait constant reified ok")
