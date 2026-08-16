# `Type[T]` denotes a class handle for the receiver's argument, not an instance of it, so a
# function-body slot declared with one is checked as a handle. An argument with no handle form at all
# -- a builtin such as `int` -- leaves null as the only value the slot can hold, which is exactly what
# the equivalent `Type[T]` member enforces; leaving that case unchecked instead would let any handle
# through a slot no handle satisfies.
class Box[T]:
	func keep_handle(value) -> Type[T]:
		var kept: Type[T] = value
		return kept


func test() -> void:
	print(Box[Node].new().keep_handle(Node) == Node)
	print(Box[Node].new().keep_handle(null))
	print(Box[int].new().keep_handle(null))
	print("class parameter type handle slot ok")
