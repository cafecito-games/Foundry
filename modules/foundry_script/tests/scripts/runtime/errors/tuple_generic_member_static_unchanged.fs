# A static tuple member has no per-instance reification, so its recorded shape stays fully erased and
# its behavior is byte-identical to what it was before parameter nodes were preserved. A static slot
# typed by a class type parameter is rejected by the analyzer outright, so a concrete tuple is the only
# reachable static form and it is validated by shape alone, with no receiver involved.
class Crate[T] extends RefCounted:
	static var pair: (int, String) = (0, "zero")


func supply(value: Variant) -> Variant:
	return value


func test() -> void:
	var crate := Crate[String].new()
	var object: Object = crate
	object.set("pair", supply((7, 8)))
	print(Crate.pair)
	object.set("pair", supply((7, 8, 9)))
	print(Crate.pair)
	object.set("pair", supply((7, "seven")))
	print(Crate.pair)
