# A tuple store never converts -- tuple elements are invariant, and a tuple value is a read-only Array
# whose identity is what gives tuples their value semantics -- so an element whose parameter resolves
# to a complete container type has to arrive already typed. The scalar `var kept: T = ...` slot
# converts instead, which is why the same untyped array is accepted there and refused here.
class Crate[T]:
	func keep(value) -> (int, T):
		var kept: (int, T) = value
		return kept


func supply(value: Variant) -> Variant:
	return value


func test() -> void:
	print(Crate[Array[int]].new().keep(supply((1, [2]))))
