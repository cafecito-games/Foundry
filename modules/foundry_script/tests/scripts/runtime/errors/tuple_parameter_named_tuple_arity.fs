# A named tuple's identity is erased at run time, so the parameter enforces its structure: the arity
# and element types the declaration gave it.
tuple Vec2(x: float, y: float)


class Receiver extends RefCounted:
	func take(point: Vec2) -> void:
		print("took ", point)


func test() -> void:
	var callback: Callable = Receiver.new().take
	callback.call(Vec2(1.0, 2.0))
	callback.call((1.0, 2.0, 3.0))
	print("unreachable")
