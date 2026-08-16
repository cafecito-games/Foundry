# The same check at a local's boundary inside a method body, which is compiled once for the declaring
# class and resolves the parameter from the frame's receiver.
class Holder[T: Type[Node]]:
	var value: T


class Wrapper[U: Node]:
	func keep(value):
		var kept: Holder[Type[U]] = value
		return kept


func test() -> void:
	var wrapper := Wrapper[Button].new()
	print(wrapper.keep(Holder[Type[Label]].new()))
