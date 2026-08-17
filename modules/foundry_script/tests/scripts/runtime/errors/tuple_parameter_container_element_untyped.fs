# An unnamed tuple literal does not take its container element types from the expected type, so the
# `[]` in `(1, [])` is an untyped Array and the store rule -- a container element has to arrive
# already typed -- rejects it. That rule already governs a local and a `return`; applying it at the
# parameter boundary too is what keeps one declared type meaning one thing everywhere, at the cost of
# rejecting a direct call the analyzer still accepts. The underlying literal-typing gap is separate.
class Receiver extends RefCounted:
	func take(pair: (int, Array[int])) -> void:
		print("took ", pair)


func test() -> void:
	var typed: Array[int] = [1, 2]
	Receiver.new().take((1, typed))
	Receiver.new().take((1, []))
	print("unreachable")
