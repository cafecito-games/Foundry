# With no surplus argument there is nothing to solve `T` from, so inference reports the existing
# unresolved-parameter diagnostic and directs the user to explicit application.
func collect[T](...values: Array[T]) -> Array[T]:
	return values


func test():
	var unconstrained := collect()
	print(unconstrained)
