class Slot[T]:
	var value: T


var handles: Slot[Type[Node]] = Slot[Type[Node]].new()
var nested: Slot[Array[Type[Node]]] = Slot[Array[Type[Node]]].new()
var specialized: Slot[Type[Slot[int]]] = Slot[Type[Slot[int]]].new()


func lookup(slot: Slot[Type[Node]]) -> Type[Node]:
	return slot.value
