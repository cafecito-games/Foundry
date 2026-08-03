# A static member flattened in from a generic trait keeps the argument the implementer fixed it with,
# and that expectation holds on every path that reaches the slot — including a dynamic write routed
# through an *instance* of the implementer rather than through the class — so a `String` written into
# an `int`-fixed slot is rejected at runtime.
trait Slotted[T]:
	static var slot: T


class Holder:
	uses Slotted[int]


func test() -> void:
	var holder := Holder.new()
	var dynamic: Variant = holder
	dynamic.slot = 5
	print(Holder.slot)

	dynamic.slot = "not an int"
	print(Holder.slot)
