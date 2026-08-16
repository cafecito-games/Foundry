# A class-parameterized return is checked at its own boundary against the receiver's reified argument,
# so contents that no `Bag[int]` could hold fail inside `corrupted()` rather than being carried out to
# the caller's conversion.
class Bag[T]:
	func corrupted(value: Variant) -> Array[T]:
		var result: Array = [value]
		return result


func test() -> void:
	var bad: Array[int] = Bag[int].new().corrupted("wrong")
	print(bad)
