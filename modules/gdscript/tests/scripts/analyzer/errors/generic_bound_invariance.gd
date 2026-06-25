# A generic upper bound is matched invariantly in its type arguments: `Stack[String]` upcasts to
# `List[String]`, which does not satisfy a `List[int]` bound even though `Stack` derives from `List`.
class List[T]:
	var head: T


class Stack[U] extends List[U]:
	pass


class Box[B: List[int]]:
	var value: B


var bad: Box[Stack[String]]


func test() -> void:
	print(bad)
