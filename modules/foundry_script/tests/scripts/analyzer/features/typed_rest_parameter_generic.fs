# A generic rest element is solved from every surplus argument, so `Array[T]` behaves like any
# other occurrence of `T` in the signature. Explicit application binds the parameter even when no
# surplus argument is written at all.
func collect[T](...values: Array[T]) -> Array[T]:
	return values


func prepend[T](first: T, ...values: Array[T]) -> Array[T]:
	return [first] + values


func first_of[T](...values: Array[Array[T]]) -> Array[T]:
	return values[0]


func test():
	var inferred: Array[int] = collect(1, 2, 3)
	print(inferred)
	var explicit_empty: Array[int] = collect[int]()
	print(explicit_empty)
	var combined: Array[String] = prepend("a", "b", "c")
	print(combined)
	# A parameter nested inside the rest element unifies structurally, like any other container.
	# The arguments are typed locals because an array literal is element-typed from the rest slot
	# only after inference has already solved it.
	var head: Array[int] = [1, 2]
	var tail: Array[int] = [3]
	var first: Array[int] = first_of(head, tail)
	print(first)
