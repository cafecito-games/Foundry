# Conversion of a runtime-erased class-parameterized return must still reject contents that do not
# fit the concrete destination instead of widening it to Variant.
class Bag[T]:
	func corrupted(value: Variant) -> Array[T]:
		var result: Array = [value]
		return result


func test() -> void:
	var bad: Array[int] = Bag[int].new().corrupted("wrong")
	print(bad)
