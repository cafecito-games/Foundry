# An explicit use-site type-argument list is matched against the generic method arity. `swap`
# declares two type parameters, so applying a single argument is an arity error.
func swap[A, B](a: A, b: B) -> void:
	print(a, b)


func test() -> void:
	swap[int](1, 2)
