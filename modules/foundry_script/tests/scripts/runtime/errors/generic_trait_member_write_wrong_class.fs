# A trait applied with a class argument (`Slotted[Node]`) reifies that class onto the implementer,
# so a dynamic write of an unrelated object is rejected at runtime.
trait Slotted[T]:
	var slot: T


class Holder:
	uses Slotted[Node]


func test() -> void:
	var holder := Holder.new()
	var dynamic: Variant = holder
	dynamic.slot = RefCounted.new()
	print(holder.slot)
