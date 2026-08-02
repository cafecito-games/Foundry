# A generic trait's type argument is reified onto the implementer, so a dynamic write of a
# `String` into a member flattened in from `Slotted[int]` is rejected at runtime.
trait Slotted[T]:
	var slot: T


class Holder:
	uses Slotted[int]


func test() -> void:
	var holder := Holder.new()
	var dynamic: Variant = holder
	dynamic.slot = "not an int"
	print(holder.slot)
