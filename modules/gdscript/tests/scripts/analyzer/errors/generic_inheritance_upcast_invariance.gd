# Upcasting through generic inheritance stays invariant in the type arguments: a
# `Stack[String]` reaches `List` as `List[String]`, which is not assignable to `List[int]`.
class List[T]:
	var head: T


class Stack[U] extends List[U]:
	pass


func test(stack: Stack[String]) -> void:
	var ints: List[int] = stack
	print(ints)
