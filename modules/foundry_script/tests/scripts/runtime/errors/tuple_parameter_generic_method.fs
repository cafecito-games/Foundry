# A method type parameter is erased in the compiled shape, exactly as it is in every other parameter,
# so the erased element accepts anything while the declared arity and the concrete `int` still hold.
class Receiver extends RefCounted:
	func take[T](pair: (int, T)) -> void:
		print("took ", pair)


func test() -> void:
	var receiver := Receiver.new()
	var callback: Callable = receiver.take
	callback.call((1, 2, 3))
	print("unreachable")
