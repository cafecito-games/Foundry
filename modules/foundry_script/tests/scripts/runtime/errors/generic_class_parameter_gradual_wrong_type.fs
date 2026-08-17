# The gradual allowance into a class type parameter survives because the slot is checked, not because
# it is trusted. A `Variant` no `Crate[int]` could hold is refused at the slot's own boundary, rather
# than travelling on to fail later at whatever operator first inspects it.
class Crate[T]:
	func keep(value: Variant) -> T:
		var kept: T = value
		return kept


func untyped_text() -> Variant:
	return "not an int"


func test() -> void:
	var crate := Crate[int].new()
	print(crate.keep(untyped_text()))
