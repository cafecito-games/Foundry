# Inferring `T` from `swap(1, "x")` binds it to both `int` and `String`, which conflict, so
# inference fails and the user is directed to apply the type arguments explicitly.
func swap[T](a: T, b: T) -> Array[T]:
	return [b, a]


func test():
	var pair := swap(1, "x")
	print(pair)
