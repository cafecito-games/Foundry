# A receiver that reified nothing resolves no argument, so the parameter element degrades to accepting
# anything rather than to a demand nothing can satisfy. The concrete elements and the arity still hold.
class Crate[T] extends RefCounted:
	var pair: (int, T) = (0, null)


func supply(value: Variant) -> Variant:
	return value


func test() -> void:
	var crate := Crate.new()
	var object: Object = crate
	object.set("pair", supply((7, 8)))
	print(crate.pair)
	object.set("pair", supply((7, "eight")))
	print(crate.pair)
	object.set("pair", supply(("seven", 8)))
	print(crate.pair)
