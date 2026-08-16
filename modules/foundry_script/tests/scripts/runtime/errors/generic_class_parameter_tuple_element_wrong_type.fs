# The parameter element of a tuple slot is resolved against the receiver's reified argument, so a
# `Crate[int]` rejects a String there. The value arrives through an untyped parameter, which the
# analyzer cannot pin down, so the slot itself is what catches it.
class Crate[T]:
	func keep(value) -> (int, T):
		var kept: (int, T) = value
		return kept


func supply(value: Variant) -> Variant:
	return value


func test() -> void:
	print(Crate[int].new().keep(supply((1, "wrong"))))
