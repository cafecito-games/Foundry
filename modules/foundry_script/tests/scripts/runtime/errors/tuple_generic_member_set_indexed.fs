# `Object.set_indexed()` reaches the same member-write leg, so it rejects the same value with the same
# sentence.
class Crate[T] extends RefCounted:
	var pair: (int, T) = (0, null)


func supply(value: Variant) -> Variant:
	return value


func test() -> void:
	var crate := Crate[String].new()
	var object: Object = crate
	object.set_indexed("pair", supply((7, 8)))
	print(crate.pair)
	object.set_indexed("pair", supply((7, "eight")))
	print(crate.pair)
