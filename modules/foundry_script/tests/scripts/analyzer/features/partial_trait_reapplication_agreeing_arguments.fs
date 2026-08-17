# A subclass may re-apply a trait its base already bound as long as the two bindings agree. Two
# composite bindings that fix the same components and leave the same position open agree, even though
# the open position names a different parameter on each side, because the chain instantiates both
# with the same argument.
trait Keeper[T]:
	func keep(value: T) -> T:
		return value


class Pair[A, B]:
	pass


class Base[U]:
	uses Keeper[Pair[int, U]]


class Child[V] extends Base[V]:
	uses Keeper[Pair[int, V]]


# An unspecialized component states nothing about a component the chain leaves on an unreified
# parameter, so re-applying `Keeper[Array]` over an inherited `Keeper[Array[U]]` is not a conflict
# either: the two bindings differ only where neither carries evidence.
class ArrayBase[U]:
	uses Keeper[Array[U]]


class ArrayChild[W] extends ArrayBase[W]:
	uses Keeper[Array]


func test() -> void:
	var child := Child[int].new()
	var slot: Keeper[Pair[int, int]] = child
	print(slot.keep(Pair[int, int].new()) != null)
	print(ArrayChild[int].new() != null)
