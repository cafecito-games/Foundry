# A handle-typed bound (`T: Type[Node]`) is satisfied by a handle whose represented type satisfies the
# bound's represented type, so the handle layer constrains rather than merely being ignored.
class Holder[T: Type[Node]]:
	var value: T


func test() -> void:
	var nodes := Holder[Type[Node]].new()
	nodes.value = Node
	print(nodes.value == Node)

	var buttons := Holder[Type[Button]].new()
	buttons.value = Button
	print(buttons.value == Button)
	print("handle bound ok")
