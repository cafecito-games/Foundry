# A call argument that is rejected at the call site must not swallow the receiver's side
# effects: the receiver is written first, so it runs before the argument is checked.
# https://github.com/cafecito-games/Foundry/issues/2255
var order: Array = []


class Box:
	func identity[T](value: T) -> T:
		return value


func make_box() -> Box:
	print("receiver ran")
	return Box.new()


func test():
	var variant: Variant = "not an int"
	print(make_box().identity[int](variant))
	print("after call")
