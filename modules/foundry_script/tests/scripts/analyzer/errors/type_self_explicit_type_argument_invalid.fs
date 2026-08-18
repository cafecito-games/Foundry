# Resolving the return's `Self` against the receiver does not relax the explicit type arguments
# themselves: a bound violation is still reported for a `Self`-returning generic method.
class Cell:
	func pick[T: Resource](value: T) -> Self:
		print(value)
		return self


func test() -> void:
	var cell := Cell.new()
	var bad := cell.pick[int](1)
	print(bad)
