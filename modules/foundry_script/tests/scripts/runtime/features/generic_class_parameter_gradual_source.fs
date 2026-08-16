# A gradual source keeps its allowance where the destination emits a check. A class type parameter is
# reified onto the instance, so an instance method's `T` slot is validated against the argument this
# receiver carries -- which is exactly what makes accepting a `Variant` here honest rather than a
# laundering hole. The value arrives converted, at the slot's own boundary.
class Crate[T]:
	func keep(value: Variant) -> T:
		var kept: T = value
		return kept

	func keep_returned(value: Variant) -> T:
		return value

	func keep_collected(value: Variant) -> Array[T]:
		var kept: Array[T] = value
		return kept


func untyped_int() -> Variant:
	return 5


func untyped_float() -> Variant:
	return 7.0


func untyped_numbers() -> Variant:
	var numbers: Array[int] = [1, 2]
	return numbers


func test() -> void:
	var crate := Crate[int].new()
	print(crate.keep(untyped_int()))
	print(crate.keep(untyped_float())) # converted to int at the slot boundary
	print(crate.keep_returned(untyped_int()))
	print(crate.keep_collected(untyped_numbers()))
	print("class parameter gradual source ok")
