# A script-class element is tested as itself, so an unrelated script instance does not satisfy it.
class Marked extends RefCounted:
	pass


class Other extends RefCounted:
	pass


class Receiver extends RefCounted:
	func take(pair: (int, Marked)) -> void:
		print("took ", pair != null)


func test() -> void:
	var callback: Callable = Receiver.new().take
	callback.call((1, Marked.new()))
	callback.call((1, Other.new()))
	print("unreachable")
