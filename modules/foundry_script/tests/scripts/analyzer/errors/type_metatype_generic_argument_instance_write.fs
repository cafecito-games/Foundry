class Factory extends RefCounted:
	pass


class Slot[T]:
	var value: T


func test():
	var slot := Slot[Type[Factory]].new()
	slot.value = Factory.new()
