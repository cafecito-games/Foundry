# A subclass that binds its base's parameter to `Type[Factory]` does not satisfy a slot expecting the
# base specialized with `Factory`: generic arguments are invariant and the handle layer participates
# in the comparison, so the leaf-to-base projection rejects the value even through an untyped alias.
class Factory extends RefCounted:
	pass


class Slot[T] extends RefCounted:
	var value: T


class HandleSlot extends Slot[Type[Factory]]:
	pass


class InstanceSlot extends Slot[Factory]:
	pass


func test() -> void:
	var instance_slots: Array[Slot[Factory]] = []
	var untyped: Array = instance_slots
	untyped.append(InstanceSlot.new())
	print(instance_slots.size())
	untyped.append(HandleSlot.new())
	print(instance_slots.size())
