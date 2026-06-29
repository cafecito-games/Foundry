# Generic-method type inference: each type parameter is solved by unifying the argument
# types against the declared parameter types, so calls resolve to concrete types without
# explicit application. Explicit type arguments short-circuit inference.
func identity[T](value: T) -> T:
	return value


# Both parameters are typed `T`; each argument constrains `T`, and consistent arguments
# (here both `int`) solve it without conflict.
func choose[T](first: T, _second: T) -> T:
	return first


func test():
	var number := identity(7)
	print(number)
	var text := identity("hi")
	print(text)
	print(identity(3.5))
	print(choose(1, 2))
	print(identity[int](9))
