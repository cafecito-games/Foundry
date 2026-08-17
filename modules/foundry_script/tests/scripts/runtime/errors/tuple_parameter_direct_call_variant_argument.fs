# No callable and no reflection: a plain direct call whose argument is `Variant`-typed. The analyzer
# rejects a statically typed wrong argument here, but it has nothing to judge a `Variant` by, so the
# callee-side boundary is what rejects this one.
class Receiver extends RefCounted:
	func take(pair: (int, String)) -> void:
		print("took ", pair)


func supply(value: Variant) -> Variant:
	return value


func test() -> void:
	Receiver.new().take(supply((1, 2, 3)))
	print("unreachable")
