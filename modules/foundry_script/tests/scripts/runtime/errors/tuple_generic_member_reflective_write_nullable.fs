# A `T?` element admits null whatever the receiver reified for `T`, so resolving the parameter keeps the
# declared nullability: `(7, null)` is accepted while `(7, 8)` is rejected against `(int, String?)`.
class Crate[T] extends RefCounted:
	var maybe: (int, T?) = (0, null)


func supply(value: Variant) -> Variant:
	return value


func test() -> void:
	var crate := Crate[String].new()
	var object: Object = crate
	object.set("maybe", supply((7, null)))
	print(crate.maybe)
	object.set("maybe", supply((7, 8)))
	print(crate.maybe)
	object.set("maybe", supply((7, "eight")))
	print(crate.maybe)
