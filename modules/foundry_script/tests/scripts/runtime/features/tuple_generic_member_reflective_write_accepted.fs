# A matching reflective write is still accepted, and its value is normalized to the canonical read-only
# carrier every tuple slot holds -- including a shape-compatible mutable Array, which is stored as a
# tuple rather than as the caller's still-writable Array.
class Crate[T] extends RefCounted:
	var pair: (int, T) = (0, null)


func supply(value: Variant) -> Variant:
	return value


func test() -> void:
	var crate := Crate[String].new()
	var object: Object = crate
	object.set("pair", supply((7, "seven")))
	print(crate.pair)
	var mutable: Array = [8, "eight"]
	object.set("pair", supply(mutable))
	print(crate.pair)
	mutable[1] = "mutated"
	print(crate.pair)
