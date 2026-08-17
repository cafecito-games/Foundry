# The check recurses: an inner tuple is tested as a tuple, so its own arity is enforced too.
class Receiver extends RefCounted:
	func take(pair: (int, (String, bool))) -> void:
		print("took ", pair)


func test() -> void:
	var callback: Callable = Receiver.new().take
	callback.call((1, ("four", true, 9)))
	print("unreachable")
