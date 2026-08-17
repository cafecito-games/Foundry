# A tuple store never converts -- tuple elements are invariant, and a tuple value is a read-only Array
# whose identity is what gives tuples their value semantics -- so an element declared as a typed
# container has to arrive already typed. Nothing about that rule is special to a type parameter: a
# resolved element is the literal element it resolves to, in both directions, so the `(int, T)` slot on
# a `Crate[Array[int]]` receiver and the literal `(int, Array[int])` slot below refuse the same value.
#
# The two accepting slots are the deliberate contrast. A container declared *around* a parameter keeps
# erasing, so `(int, Array[T])` demands only an Array and leaves its contents unchecked; and the scalar
# `var kept: T` slot converts, retyping the array it stores. That asymmetry is the documented cost of a
# store that never converts inside a tuple.
class Crate[T]:
	func keep_resolved(value) -> Variant:
		var kept: (int, T) = value
		return kept

	func keep_declared_container(value) -> Variant:
		var kept: (int, Array[T]) = value
		return kept

	func keep_scalar(value) -> Variant:
		var kept: T = value
		return kept


func keep_literal(value) -> Variant:
	var kept: (int, Array[int]) = value
	return kept


func supply(value: Variant) -> Variant:
	return value


func describe(value: Variant) -> String:
	if value is Array:
		return str(value) + " typed: " + str((value as Array).is_typed())
	return str(value)


func test() -> void:
	var crate := Crate[Array[int]].new()
	print("resolved element: ", crate.keep_resolved(supply((1, [2]))))
	print("literal element: ", keep_literal(supply((1, [2]))))

	var declared: Variant = crate.keep_declared_container(supply((1, [2])))
	print("declared container element: ", describe((declared as Array)[1]))
	print("scalar slot: ", describe(crate.keep_scalar(supply([2]))))
