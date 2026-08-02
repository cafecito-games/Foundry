# A specialized class handle used as a generic argument keeps its own type arguments, so
# `Slot[Type[Box[int]]]` accepts only the `Box[int]` handle.
class Box[T] extends RefCounted:
	var value: T


class Slot[T]:
	var value: T


func test() -> void:
	var slot: Slot[Type[Box[int]]] = Slot[Type[Box[int]]].new()
	slot.value = Box[int]
	print(slot.value == Box[int])
	var made := slot.value.new()
	print(made is Box)
	print("specialized handle argument ok")
