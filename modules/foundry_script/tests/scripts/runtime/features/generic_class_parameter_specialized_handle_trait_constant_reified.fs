# A constant declared inside a generic trait names the trait's own type parameters, and the arguments
# the implementer applied are known by the time the flattened copy is folded. Substituting them first
# lets a concrete application fold the specialized handle it really means, recursively through a
# composite argument, instead of baking the parameter's erasure in as the definite specialization
# `Variant`. A constant whose arguments are not all concrete after substitution stays the bare
# unspecialized handle -- a constant pool has no receiver, and a specialized handle has no
# representation for a partially known argument vector, so one forwarded argument leaves the whole
# handle bare.
#
# An argument that names `Self` is receiver-dependent: constructing through the constant's
# name resolves `Self` against the receiver, so a subclass of the implementer builds its own
# specialization. The same rule governs both spellings of such an argument -- one the implementer
# applied (`uses Aliasing[Self]`) and one the trait wrote directly in the constant
# (`const Direct = Holder[Self]`), in a generic and a non-generic trait alike.
# The flattened constant is one slot in the implementer's pool shared by every
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


trait DirectSelfAliasing:
	const Direct = Holder[Self]
	const DirectNested = Holder[Array[Self]]

	# The trait's own default method names the flattened constant without a receiver, which resolves
	# through a different path than the qualified spelling and must report the same value form.
	func own_direct() -> Variant:
		return Direct


trait GenericDirectSelfAliasing[W]:
	const Direct = Holder[Self]
	const DirectNested = Holder[Array[Self]]
	# One argument the implementer supplied and one that stands for the receiver: the receiver-dependent
	# position governs the whole handle, exactly as a forwarded parameter does.
	const DirectPaired = Pair[W, Self]


class DirectSelf:
	uses DirectSelfAliasing


class SubDirectSelf extends DirectSelf:
	pass


# A subclass inherits the implementer's one slot rather than getting one of its own, so a body here
# that names the constant must report what that slot holds, not this class's own `Self`. A class that
# flattens a constant in and is then extended cannot be `final`, so the slot is always the bare handle.
final class FinalSubDirectSelf extends DirectSelf:
	func read_inherited() -> Variant:
		return Direct


final class FinalDirectSelf:
	uses DirectSelfAliasing


class GenericDirectSelf:
	uses GenericDirectSelfAliasing[int]


class SubGenericDirectSelf extends GenericDirectSelf:
	pass


final class FinalGenericDirectSelf:
	uses GenericDirectSelfAliasing[int]


# `Self` in a constant an ordinary class declares means the declaring class, receiver-independently,
# and is unaffected by the trait rule.
class PlainSelf:
	const Aliased = Holder[Self]


class SubPlainSelf extends PlainSelf:
	pass


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

	# A `Self` the trait wrote directly in the constant, through a NON-generic trait. The construction
	# form is receiver-dependent, so a subclass of the implementer builds its own specialization, and a
	# statically typed slot accepts it. The stored handle is bare for both receivers: one slot cannot
	# assert a specialization either receiver contradicts.
	var direct_built: Variant = DirectSelf.Direct.new()
	print(direct_built is Holder[DirectSelf])
	# A statically typed slot accepts the construction form, which the erased `Holder[Self]` static type
	# made impossible before the receiver substitution.
	var typed_direct: Holder[SubDirectSelf] = SubDirectSelf.Direct.new()
	var typed_direct_value: Variant = typed_direct
	print(typed_direct_value is Holder[SubDirectSelf])
	print(typed_direct_value is Holder[DirectSelf])
	var direct_stored: Variant = DirectSelf.Direct
	print(direct_stored == Holder)
	print(direct_stored == Holder[DirectSelf])
	var sub_direct_stored: Variant = SubDirectSelf.Direct
	print(sub_direct_stored == Holder)
	print(sub_direct_stored == Holder[SubDirectSelf])
	# Constructing through the stored handle now agrees with the name form on carrying no
	# specialization, rather than asserting one a `Holder[SubDirectSelf]` slot rejects.
	@warning_ignore("UNSAFE_METHOD_ACCESS")
	var from_sub_direct_stored: Variant = sub_direct_stored.new()
	print(from_sub_direct_stored is Holder)
	print(from_sub_direct_stored is Holder[DirectSelf])
	print(DirectSelf.new().own_direct() == Holder)
	print(DirectSelf.new().own_direct() == Holder[DirectSelf])
	# The inherited slot is the implementer's, so a `final` subclass reading it in a body agrees with the
	# stored value rather than folding its own `Self` in.
	var inherited_slot: Variant = FinalSubDirectSelf.Direct
	print(inherited_slot == Holder)
	print(FinalSubDirectSelf.new().read_inherited() == Holder)
	print(FinalSubDirectSelf.new().read_inherited() == Holder[FinalSubDirectSelf])
	# The name form stays receiver-dependent for that subclass, as for any other.
	var final_sub_built: Variant = FinalSubDirectSelf.Direct.new()
	print(final_sub_built is Holder[FinalSubDirectSelf])

	var direct_nested_built: Variant = SubDirectSelf.DirectNested.new()
	print(direct_nested_built is Holder[Array[SubDirectSelf]])
	print(direct_nested_built is Holder[Array[DirectSelf]])
	var direct_nested_stored: Variant = SubDirectSelf.DirectNested
	print(direct_nested_stored == Holder)
	print(direct_nested_stored == Holder[Array[DirectSelf]])

	# The same trait, a `final` implementer: exactly one receiver is possible, so both spellings agree.
	var final_direct_built: Variant = FinalDirectSelf.Direct.new()
	print(final_direct_built is Holder[FinalDirectSelf])
	var final_direct_stored: Variant = FinalDirectSelf.Direct
	print(final_direct_stored == Holder[FinalDirectSelf])
	print(final_direct_stored == Holder)
	var final_direct_nested: Variant = FinalDirectSelf.DirectNested
	print(final_direct_nested == Holder[Array[FinalDirectSelf]])
	print(FinalDirectSelf.new().own_direct() == Holder[FinalDirectSelf])

	# A `Self` written directly in a GENERIC trait's constant follows the same rule, alongside the
	# arguments the implementer applied.
	var generic_direct_built: Variant = SubGenericDirectSelf.Direct.new()
	print(generic_direct_built is Holder[SubGenericDirectSelf])
	print(generic_direct_built is Holder[GenericDirectSelf])
	var generic_direct_stored: Variant = GenericDirectSelf.Direct
	print(generic_direct_stored == Holder)
	print(generic_direct_stored == Holder[GenericDirectSelf])
	var sub_generic_direct_stored: Variant = SubGenericDirectSelf.Direct
	print(sub_generic_direct_stored == Holder)
	print(sub_generic_direct_stored == Holder[SubGenericDirectSelf])
	@warning_ignore("UNSAFE_METHOD_ACCESS")
	var from_sub_generic_stored: Variant = sub_generic_direct_stored.new()
	print(from_sub_generic_stored is Holder)
	print(from_sub_generic_stored is Holder[GenericDirectSelf])
	var generic_direct_nested: Variant = SubGenericDirectSelf.DirectNested
	print(generic_direct_nested == Holder)
	print(generic_direct_nested == Holder[Array[GenericDirectSelf]])
	var generic_direct_paired: Variant = GenericDirectSelf.DirectPaired
	print(generic_direct_paired == Pair)
	print(generic_direct_paired == Pair[int, GenericDirectSelf])

	var final_generic_direct: Variant = FinalGenericDirectSelf.Direct
	print(final_generic_direct == Holder[FinalGenericDirectSelf])
	print(FinalGenericDirectSelf.Direct.new() is Holder[FinalGenericDirectSelf])
	var final_generic_nested: Variant = FinalGenericDirectSelf.DirectNested
	print(final_generic_nested == Holder[Array[FinalGenericDirectSelf]])
	var final_generic_paired: Variant = FinalGenericDirectSelf.DirectPaired
	print(final_generic_paired == Pair[int, FinalGenericDirectSelf])

	# An ordinary class's own `Self` constant is receiver-independent and stays fully specialized on the
	# declaring class, for the declaring class and for a subclass receiver alike.
	var plain_stored: Variant = PlainSelf.Aliased
	print(plain_stored == Holder[PlainSelf])
	var sub_plain_stored: Variant = SubPlainSelf.Aliased
	print(sub_plain_stored == Holder[PlainSelf])
	var plain_built: Variant = SubPlainSelf.Aliased.new()
	print(plain_built is Holder[PlainSelf])
	print("trait constant reified ok")
