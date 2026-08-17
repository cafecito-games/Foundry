# A constant declared inside a generic trait names the trait's own type parameters, and the arguments
# the implementer applied are known by the time the flattened copy is folded. Substituting them first
# lets a concrete application fold the specialized handle it really means, recursively through a
# composite argument, instead of baking the parameter's erasure in as the definite specialization
# `Variant`. A constant whose arguments are not all concrete after substitution stays the bare
# unspecialized handle -- a constant pool has no receiver, and a specialized handle has no
# representation for a partially known argument vector, so one forwarded argument leaves the whole
# handle bare.
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

	# An implementer that applies the trait with `Self` reifies through the constant's name, where the
	# construction resolves `Self` to the implementer. Reading the same constant back as a value lands
	# on the bare handle instead: the argument still stands for a parameter when the constant is folded,
	# so it carries no evidence rather than evidence the constant cannot honestly claim.
	var self_applied: Variant = SelfApplied.Aliased.new()
	print(self_applied is Holder[SelfApplied])
	print(self_applied is Holder[Variant])
	var self_stored: Variant = SelfApplied.Aliased
	@warning_ignore("UNSAFE_METHOD_ACCESS")
	var from_self_stored: Variant = self_stored.new()
	print(from_self_stored is Holder)
	print(from_self_stored is Holder[Variant])

	print(ConcretePairing.new().local_handle() == Pair[int, String])
	print(ConcretePairing.new().local_handle() == Pair)
	print(MixedPairing[String].new().local_handle() == Pair)
	print(MixedPairing[String].new().local_handle() == Pair[int, String])
	print("trait constant reified ok")
