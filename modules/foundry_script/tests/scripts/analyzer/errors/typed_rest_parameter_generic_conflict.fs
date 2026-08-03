# Inference consumes every surplus argument, so a later disagreeing rest value conflicts with the
# element type the earlier ones already solved.
func collect[T](...values: Array[T]) -> Array[T]:
	return values


func test():
	var bad := collect(1, 2, "three")
	print(bad)
