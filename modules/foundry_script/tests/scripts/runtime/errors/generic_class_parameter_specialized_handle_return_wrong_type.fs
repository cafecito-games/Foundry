# The return boundary is checked in its own right: the caller stores the result in an untyped local,
# so nothing downstream would have rejected the wrongly specialized handle.
class Holder[T: Type[Node]]:
	var value: T


class Wrapper[U: Node]:
	func produce(value) -> Holder[Type[U]]:
		return value


func test() -> void:
	var wrapper := Wrapper[Button].new()
	var produced = wrapper.produce(Holder[Type[Label]].new())
	print(produced)
