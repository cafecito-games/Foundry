# A tuple member whose declared type names a class type parameter carries a type-argument binding, but
# that binding is the same erased Array its slot is. The declared shape is what enforces the arity and
# the concrete `int` beside the parameter, and it resolves the parameter element against the receiver's
# reified argument. A reflective rejection reports the same sentence the in-body store does.
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
	object.set("pair", (2, 3, 4))
	print(crate.pair)
	crate.store(supply((3, 4, 5)))
	print(crate.pair)
