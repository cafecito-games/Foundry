# A surplus argument enters the same erased destination a fixed parameter does, so once the rest
# element type is resolved at the call site each element is checked there too.
func collect[T](...values: Array[T]) -> Array[T]:
	print("callee entered")
	return values


func untyped_text() -> Variant:
	return "not an int"


func test() -> void:
	print(collect[int](1, untyped_text()))
