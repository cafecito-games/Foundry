# A generic trait's type argument is reified onto the implementer's static members too, so a
# dynamic write of a `String` into a static member flattened in from `Slotted[int]` is rejected at
# runtime, whether written through an instance or through the class itself.
trait Slotted[T]:
	static var slot: T


class Holder:
	uses Slotted[int]


func test() -> void:
	Holder.slot = 5
	print(Holder.slot)

	var dynamic: Variant = Holder
	dynamic.slot = "not an int"
	print(Holder.slot)
