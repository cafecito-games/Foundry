# A tuple slot carries its shape to run time as a compiled descriptor, and an element that names a
# class type parameter is resolved against the argument this receiver reified rather than accepted as
# anything. Every other element keeps being checked structurally, so the two halves of `(int, T)` are
# enforced by the same store.
#
# An element the receiver cannot resolve degrades on its own: a raw, unspecialized instance and an
# ancestor step that supplied nothing each leave only that element gradual.
class Crate[T]:
	func keep(value) -> (int, T):
		var kept: (int, T) = value
		return kept

	func replace(first, second) -> (int, T):
		var kept: (int, T) = first
		kept = second
		return kept

	func keep_erased_element(value) -> (int, Array[T]):
		# A container *declared* around a parameter erases its element typing on purpose -- its concrete
		# consumer is what retypes it -- so this element still takes an Array of anything, while the
		# `int` beside it is enforced.
		var kept: (int, Array[T]) = value
		return kept

	func keep_nested(value) -> (int, (String, T)):
		var kept: (int, (String, T)) = value
		return kept

	func only_nullable(value) -> void:
		# A tuple element carries its own "or null" into the compiled descriptor, so a nullable element
		# is evidence like any other and this slot is receiver-relative on its own -- it takes no
		# sibling parameter element to make it one.
		var kept: (int, T?) = value
		print(kept)


# An inherited body resolves against the leaf's specialization, not against the parameter the
# declaring class left open.
class IntCrate extends Crate[int]:
	pass


# `HalfPair` fixes `Pair`'s `K` through `extends` and leaves its own `V` open, so one element of the
# same tuple is checked and the other stays gradual.
class Pair[K, V]:
	func keep_both(value) -> (K, V):
		var kept: (K, V) = value
		return kept


class HalfPair[V] extends Pair[int, V]:
	pass


# A receiver can prove part of an argument and leave the rest open: `NestingCrate` fixes `ArrayCrate`'s
# `T` to `Array[U]` through `extends` while its own `U` stays unsupplied on a raw instance. A
# structural tuple test has no per-node gradual form -- an element type is either enforced whole or
# not at all -- so evidence short of complete degrades that element instead of hardening "some Array"
# into `Array[Variant]`, which would reject the untyped arrays the projection never contradicted.
class ArrayCrate[T]:
	func keep_array(value) -> (int, T):
		var kept: (int, T) = value
		return kept


class NestingCrate[U] extends ArrayCrate[Array[U]]:
	pass


# A nullable element admits null whatever its parameter turns out to be, so the declared nullability
# travels with the node and is applied after the receiver resolves it. Both spellings behave the same:
# whether a sibling element names a parameter or not changes nothing about how the nullable one is
# checked -- see `only_nullable()` above for the no-sibling case.
class OptionalPair[K, V]:
	func keep_optional(value) -> (K, V?):
		var kept: (K, V?) = value
		return kept


# A trait body names the TRAIT's parameters, so its slots are resolved through the arguments each
# implementer applied rather than through the implementer's own parameter list.
trait TupleKeeper[V]:
	func keep_via_trait(value) -> (int, V):
		var kept: (int, V) = value
		return kept


class ForwardingTupleCrate[W]:
	uses TupleKeeper[W]


class ConcreteTupleCrate:
	uses TupleKeeper[String]


# A nullable element in a trait body keeps its "or null" through substitution: learning what the
# trait's parameter stands for says nothing about whether the slot admits null, so a concrete
# application enforces the argument and still takes null, exactly as a forwarded one does.
trait OptionalTupleKeeper[V]:
	func keep_optional_via_trait(value) -> void:
		var kept: (int, V?) = value
		print(kept)


class ConcreteOptionalTupleCrate:
	uses OptionalTupleKeeper[String]


class ForwardingOptionalTupleCrate[W]:
	uses OptionalTupleKeeper[W]


func supply(value: Variant) -> Variant:
	return value


func test() -> void:
	var crate := Crate[int].new()
	print(crate.keep(supply((1, 2))))
	print(crate.replace(supply((1, 2)), supply((3, 4))))
	print(crate.keep_erased_element(supply((1, ["not an int", true]))))
	print(crate.keep_nested(supply((1, ("two", 3)))))

	# The passing value is stored unchanged, keeping the read-only carrier a tuple value rests on.
	var kept := crate.keep(supply((5, 6)))
	print(kept.0)
	print(kept.1)
	print(kept == (5, 6))

	print(IntCrate.new().keep(supply((7, 8))))

	# `K` is fixed to `int` by `extends`; `V` has no argument on this receiver, so only that element
	# stays gradual.
	var half := HalfPair.new()
	print(half.keep_both(supply((9, "anything"))))

	print(NestingCrate.new().keep_array(supply((1, [2]))))

	# A complete container argument is enforced exactly. The store never converts, so the element has
	# to arrive already typed -- see `generic_class_parameter_tuple_untyped_container.fs`.
	var typed_contents: Array[int] = [3]
	print(ArrayCrate[Array[int]].new().keep_array(supply((1, typed_contents))))

	# A slot whose only parameter element is nullable is receiver-relative on its own: `null` still
	# passes, and a correctly typed value is accepted once the receiver resolves the element.
	var string_crate := Crate[String].new()
	string_crate.only_nullable(supply((1, null)))
	string_crate.only_nullable(supply((2, "two")))

	var optional := OptionalPair[int, String].new()
	print(optional.keep_optional(supply((1, null))))
	print(optional.keep_optional(supply((2, "two"))))

	print(ForwardingTupleCrate[String].new().keep_via_trait(supply((10, "ten"))))
	print(ConcreteTupleCrate.new().keep_via_trait(supply((11, "eleven"))))

	var concrete_optional := ConcreteOptionalTupleCrate.new()
	concrete_optional.keep_optional_via_trait(supply((13, null)))
	concrete_optional.keep_optional_via_trait(supply((14, "fourteen")))
	var forwarding_optional := ForwardingOptionalTupleCrate[String].new()
	forwarding_optional.keep_optional_via_trait(supply((15, null)))
	forwarding_optional.keep_optional_via_trait(supply((16, "sixteen")))

	# A raw, un-parameterized receiver carries no reified argument, so the parameter element accepts
	# anything while the arity and the `int` are still enforced.
	var raw := Crate.new()
	print(raw.keep(supply((12, "anything"))))
	print("class parameter tuple slot ok")
