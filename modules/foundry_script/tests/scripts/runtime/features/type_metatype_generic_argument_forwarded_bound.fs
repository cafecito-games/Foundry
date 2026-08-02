# A handle-typed bound is checked against a handle-wrapped type parameter without first unwrapping the
# parameter to its own bound, so `Type[U]` with `U: Node` satisfies `T: Type[Node]`.
class Holder[T: Type[Node]]:
	var value: T


class Wrapper[U: Node]:
	var holder: Holder[Type[U]] = Holder[Type[U]].new()


func test() -> void:
	var wrapper := Wrapper[Button].new()
	wrapper.holder.value = Button
	print(wrapper.holder.value == Button)
	print("forwarded handle bound ok")
