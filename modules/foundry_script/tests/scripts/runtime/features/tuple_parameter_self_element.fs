# A `Self` element in a tuple parameter now reaches run time, so the signature references `Self` and
# the frame resolves it per call: an instance frame against the receiver's leaf script, a static frame
# against the class handle the call was made through. A legal call always supplies one, so the
# hard-fail path for a static frame with no receiver stays unreachable here.
class Node2 extends RefCounted:
	func take(pair: (int, Self)) -> void:
		print("instance took ", pair.0, " ", pair.1 != null)

	static func make(pair: (int, Self)) -> void:
		print("static took ", pair.0, " ", pair.1 != null)


class Derived extends Node2:
	pass


func supply(value: Variant) -> Variant:
	return value


func test() -> void:
	var node := Node2.new()
	node.take(supply((1, Node2.new())))
	var callback: Callable = node.take
	callback.call((2, Node2.new()))

	var static_callback := Callable(Node2, "make")
	static_callback.call((3, Node2.new()))

	# An inherited instance method resolves `Self` against the receiver's leaf, so a `Derived` receiver
	# demands a `Derived` element.
	var derived := Derived.new()
	derived.take(supply((4, Derived.new())))
