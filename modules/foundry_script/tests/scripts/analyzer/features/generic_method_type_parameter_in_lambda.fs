# An enclosing generic method's type parameters are visible inside a lambda body, including in the
# lambda's own parameter and return annotations and through nested lambdas. The lambda's function
# has no type parameters of its own, so resolution walks out to the enclosing method.
func identity[T](value: T) -> T:
	var echo := func(inner: T) -> T:
		return inner
	return echo.call(value)


func nested[T](value: T) -> T:
	var once := func(a: T) -> T:
		var twice := func(b: T) -> T:
			return b
		return twice.call(a)
	return once.call(value)


func test() -> void:
	print(identity[int](7))
	print(nested[String]("hi"))
