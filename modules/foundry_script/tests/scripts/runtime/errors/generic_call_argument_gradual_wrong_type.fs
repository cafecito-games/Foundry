# The gradual allowance into an erased method type parameter has to be backed by a check somewhere.
# The callee cannot perform it -- its `value: T` slot is erased -- so the caller does, against the type
# it substituted. The callee's own side effect proves the rejection happens before the body runs.
func identity[T](value: T) -> T:
	print("callee entered")
	return value


func untyped_text() -> Variant:
	return "not an int"


func test() -> void:
	print(identity[int](untyped_text()))
