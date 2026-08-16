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


# A nullable element admits null whatever its parameter turns out to be, so the declared nullability
# travels with the node and is applied after the receiver resolves it. A slot whose *only* parameter
# element is nullable keeps no evidence at all -- the same limitation a nullable member binding has --
# so it takes a sibling element to make this shape receiver-relative in the first place.
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

	var optional := OptionalPair[int, String].new()
	print(optional.keep_optional(supply((1, null))))
	print(optional.keep_optional(supply((2, "two"))))

	print(ForwardingTupleCrate[String].new().keep_via_trait(supply((10, "ten"))))
	print(ConcreteTupleCrate.new().keep_via_trait(supply((11, "eleven"))))

	# A raw, un-parameterized receiver carries no reified argument, so the parameter element accepts
	# anything while the arity and the `int` are still enforced.
	var raw := Crate.new()
	print(raw.keep(supply((12, "anything"))))
	print("class parameter tuple slot ok")
