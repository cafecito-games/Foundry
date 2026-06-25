# A type argument satisfies a generic upper bound when it upcasts to the bound's exact
# specialization: `Stack[int]` is a `List[int]`, and `IntList extends List[int]` is too.
class List[T]:
	var head: T


class Stack[U] extends List[U]:
	pass


class IntList extends List[int]:
	pass


class Box[B: List[int]]:
	var value: B


var matching: Box[Stack[int]]
var inherited: Box[IntList]


func test() -> void:
	print(matching)
	print(inherited)
	print("generic bound invariance ok")
