# `Type[T]` denotes a class handle for the receiver's argument, not an instance of it, so a
# function-body slot declared with one is checked as a handle. An argument with no handle form at all
# -- a builtin such as `int` -- leaves null as the only value the slot can hold, which is exactly what
# the equivalent `Type[T]` member enforces; leaving that case unchecked instead would let any handle
# through a slot no handle satisfies.
class Box[T]:
	func keep_handle(value) -> Type[T]:
		var kept: Type[T] = value
		return kept


# The handle layer travels beside the shape rather than inside it, so it survives the substitution a
# trait body goes through: applying the trait with a builtin leaves a slot only null can satisfy, and
# applying it with a class keeps the handle check.
trait HandleKeeper[V]:
	func keep_handle(value) -> Type[V]:
		var kept: Type[V] = value
		return kept


class IntHandleKeeper:
	uses HandleKeeper[int]


class NodeHandleKeeper:
	uses HandleKeeper[Node]


func test() -> void:
	print(Box[Node].new().keep_handle(Node) == Node)
	print(Box[Node].new().keep_handle(null))
	print(Box[int].new().keep_handle(null))
	print(NodeHandleKeeper.new().keep_handle(Node) == Node)
	print(IntHandleKeeper.new().keep_handle(null))
	print("class parameter type handle slot ok")
