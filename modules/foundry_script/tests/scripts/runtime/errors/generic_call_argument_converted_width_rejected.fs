# Converting an implicitly converted generic argument at the call site produces the substituted
# type's carrier, and the declared-width re-check then rejects a value the substituted width cannot
# hold — before the callee body runs, exactly as a concrete `int` parameter rejects it.
func identity[T](value: T) -> T:
	print("callee entered")
	return value


func test() -> void:
	var big: float = 2147483648.0
	print("before call")
	var _converted: int = identity[int](big)
	print("unreachable")
