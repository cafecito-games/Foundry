# The issue's original case: a tuple element that resolves to a class type parameter is the literal
# type the receiver reified it to, so a `Crate[Pair[int, String]]` receiver rejects a `Pair[int, Node]`
# in that element. The tuple store is gradual, so this rejects on evidence -- the value really does
# carry a conflicting specialization -- rather than on the absence of it.
class Pair[A, B]:
	pass


class Crate[T]:
	func keep(value) -> (int, T):
		var kept: (int, T) = value
		return kept


func supply(value: Variant) -> Variant:
	return value


func test() -> void:
	var crate := Crate[Pair[int, String]].new()
	var accepted := crate.keep(supply((1, Pair[int, String].new())))
	print("correct specialization accepted: ", accepted.0)
	var gradual := crate.keep(supply((2, Pair.new())))
	print("unspecialized accepted: ", gradual.0)
	var rejected: Variant = crate.keep(supply((3, Pair[int, Node].new())))
	# The store aborted the callee, so the caller sees the function's default return value.
	print("wrongly specialized rejected at the store: ", rejected)
