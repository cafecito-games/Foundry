# The return slot is checked at its own boundary, against this receiver's reified argument. The
# caller binds the result as `Variant`, so nothing downstream would ever look at the shape; the
# failure is reported inside `corrupted()`.
class Crate[T]:
	func corrupted(value: Variant) -> (int, T):
		return value


func test() -> void:
	var escaped: Variant = Crate[int].new().corrupted((1, "wrong"))
	print(escaped)
