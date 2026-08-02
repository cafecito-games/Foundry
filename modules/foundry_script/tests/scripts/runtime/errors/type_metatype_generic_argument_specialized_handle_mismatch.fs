# A `Slot[Type[Box[int]]]` member rejects a `Box[String]` handle at runtime: the reified argument
# carries the nested specialization, and generic arguments are invariant.
class Box[T] extends RefCounted:
	var value: T


class Slot[T]:
	var value: T


func test() -> void:
	var slot := Slot[Type[Box[int]]].new()
	slot.value = Box[int]
	var dynamic: Variant = slot
	dynamic.value = Box[String]
	print(slot.value == Box[int])
