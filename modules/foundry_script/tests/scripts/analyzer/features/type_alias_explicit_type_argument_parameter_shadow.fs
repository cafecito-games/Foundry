# A generic function's own type parameter shadows a same-spelled file-scope alias in explicit
# type-argument position, exactly as it does in an annotation (`resolve_datatype`'s rule that a
# type parameter shadows any other name). `T` inside `forward` names `forward`'s own parameter
# handle, never the file-scope `type T = int` alias, even though both are in scope.
type T = int


func identity[T](value: T) -> T:
	return value


func forward[T](value: T) -> T:
	return identity[T](value)


func test():
	print(forward("text"))
	print(forward(3.5))
