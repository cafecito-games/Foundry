# A static trait member fixed by a dependent argument (`Slotted[Box[V]]`, where `V` is still open at
# `uses` time) erases its nested `V` to an unrecoverable `Variant`, but the outer `Box` shape stays
# known and sound to check: a dynamic write of an unrelated class is still rejected at runtime, even
# though a value with the right outer class but an unverifiable `V` (see the acceptance fixture) is
# accepted.
class Box[V]:
	pass


class NotBox:
	pass


trait Slotted[T]:
	static var slot: T


class Holder[V]:
	uses Slotted[Box[V]]


func test() -> void:
	var holder := Holder[int].new()
	var dynamic: Variant = holder
	dynamic.slot = NotBox.new()
	print(holder.slot)
