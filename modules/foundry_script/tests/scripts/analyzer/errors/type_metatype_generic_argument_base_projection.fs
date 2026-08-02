class Factory extends RefCounted:
	pass


class Slot[T]:
	var value: T


class HandleSlot extends Slot[Type[Factory]]:
	pass


func accept_instances(slot: Slot[Factory]) -> void:
	print(slot)


func test():
	accept_instances(HandleSlot.new())
