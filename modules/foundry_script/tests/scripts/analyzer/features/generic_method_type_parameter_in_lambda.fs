# An enclosing generic method's type parameters are visible inside a lambda body, including in the
# lambda's own parameter and return annotations and through nested lambdas. The lambda's function
# has no type parameters of its own, so resolution walks out to the enclosing method.
#
# `Callable.call()` answers `Variant`, and a method type parameter is erased before the callee runs,
# so nothing can check such a result back into a `T` slot. These functions therefore hand the result
# on as the `Variant` it is; see `analyzer/errors/type_parameter_gradual_source_rejected.fs` for the
# rejection that closes the alternative.
func identity[T](value: T) -> Variant:
	var echo := func(inner: T) -> T:
		return inner
	return echo.call(value)


func nested[T](value: T) -> Variant:
	var once := func(a: T) -> Variant:
		var twice := func(b: T) -> T:
			return b
		return twice.call(a)
	return once.call(value)


func test() -> void:
	print(identity[int](7))
	print(nested[String]("hi"))
