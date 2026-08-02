class Factory extends RefCounted:
	pass


class Slot[T]:
	var value: T


func test():
	var handles := Slot[Type[Factory]].new()
	var instances: Slot[Factory] = handles
	print(instances)
