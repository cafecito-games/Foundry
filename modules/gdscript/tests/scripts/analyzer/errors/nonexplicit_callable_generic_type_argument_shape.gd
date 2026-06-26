# The non-explicit Callable path matches the explicit path's strictness for generic type arguments,
# not only for nested Callable/Signal signatures: a function reference taking `Box[int]` cannot satisfy
# a target whose Callable parameter is `Box[String]`. On the explicit path this mismatch was already
# rejected; the function-reference path now agrees instead of erasing the type argument.
class Box[T]:
	var value: T


func takes_int_box(_box: Box[int]) -> void:
	pass


func test() -> void:
	var handler: Callable[[Box[String]], void] = takes_int_box
	print(handler)
