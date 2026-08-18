# An inherited member's shape is re-specialized through the `extends` step alongside its binding, so a
# subclass that fixes the parameter enforces the fixed argument rather than indexing its own (empty)
# parameter list.
class Crate[T] extends RefCounted:
	var pair: (int, T) = (0, null)


class FixedCrate extends Crate[String]:
	pass


func supply(value: Variant) -> Variant:
	return value


func test() -> void:
	var fixed := FixedCrate.new()
	var object: Object = fixed
	object.set("pair", supply((7, 8)))
	print(fixed.pair)
	object.set("pair", supply((7, "eight")))
	print(fixed.pair)
