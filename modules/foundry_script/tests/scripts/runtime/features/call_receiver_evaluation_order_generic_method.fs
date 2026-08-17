# `receiver.method[T](arg)` evaluates the receiver before the argument, like any other call.
# https://github.com/cafecito-games/Foundry/issues/2255

var order: Array = []


class Box:
	func identity[T](value: T) -> T:
		return value


func note(label: String, value: int) -> int:
	order.append(label)
	return value


func make_box() -> Box:
	order.append("receiver")
	return Box.new()


func test():
	prints("value", make_box().identity[int](note("argument", 7)))
	prints("eval", order)
