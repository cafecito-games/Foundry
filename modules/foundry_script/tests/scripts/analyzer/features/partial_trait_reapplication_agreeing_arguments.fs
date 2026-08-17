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


func test() -> void:
	var child := Child[int].new()
	var slot: Keeper[Pair[int, int]] = child
	print(slot.keep(Pair[int, int].new()) != null)
