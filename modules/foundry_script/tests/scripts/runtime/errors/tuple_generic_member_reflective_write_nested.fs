# The resolution walks the whole tuple spine, so a parameter nested inside a tuple element is resolved
# too: the inner `T` rejects an `int` while a matching value is accepted.
class Crate[T] extends RefCounted:
	var nested: (int, (String, T)) = (0, ("", null))


func supply(value: Variant) -> Variant:
	return value


func test() -> void:
	var crate := Crate[String].new()
	var object: Object = crate
	object.set("nested", supply((7, ("a", 8))))
	print(crate.nested)
	object.set("nested", supply((7, ("a", "b"))))
	print(crate.nested)
