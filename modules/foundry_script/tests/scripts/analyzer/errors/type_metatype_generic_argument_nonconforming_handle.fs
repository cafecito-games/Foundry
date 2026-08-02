class Factory extends RefCounted:
	pass


class Unrelated extends RefCounted:
	pass


class Slot[T]:
	var value: T


func test():
	var slot := Slot[Type[Factory]].new()
	slot.value = Unrelated
