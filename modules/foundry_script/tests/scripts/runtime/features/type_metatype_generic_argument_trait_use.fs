# A generic trait applied with a handle-wrapped argument (`uses Slotted[Type[U]]`) keeps working: the
# flattened member is typed by the trait's parameter, whose ordinal does not index the implementer's
# reified arguments, so the slot must not be validated against the implementer's own argument.
trait Slotted[T]:
	var slot: T


class Holder[U] uses Slotted[Type[U]]:
	pass


func test() -> void:
	var holder := Holder[Node].new()
	holder.slot = Node
	print(holder.slot == Node)
	print("trait use with handle argument ok")
