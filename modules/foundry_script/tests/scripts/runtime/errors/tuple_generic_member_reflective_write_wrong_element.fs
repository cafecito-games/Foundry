# A tuple member whose declared type names a class type parameter keeps that parameter in its recorded
# shape, and a reflective write resolves it against the receiver's reified argument. A `Crate[String]`
# therefore rejects `(7, 8)` through `Object.set()` exactly as the in-body store does, reports the same
# sentence, and leaves the member holding its previous value.
class Crate[T] extends RefCounted:
	var pair: (int, T) = (0, null)

	func store(value: Variant) -> void:
		pair = value


func supply(value: Variant) -> Variant:
	return value


func test() -> void:
	var crate := Crate[String].new()
	crate.store(supply((1, "one")))
	print(crate.pair)
	var object: Object = crate
	object.set("pair", supply((7, 8)))
	print(crate.pair)
	crate.store(supply((7, 8)))
	print(crate.pair)
