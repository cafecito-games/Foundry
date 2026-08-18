# The chain check a retroactive conformance goes through compares composite bindings component by
# component too: the chain fixes `Pair[int, V]` and the conformance records `Pair[String, float]`, so
# the conformance is rejected on the component both sides fix.
trait Keeper[T]:
	func keep(value: T) -> T:
		return value


class Pair[A, B]:
	pass


class PartialBase[U]:
	uses Keeper[Pair[int, U]]


class PartialChild[V] extends PartialBase[V]:
	pass


extend PartialChild uses Keeper[Pair[String, float]]:
	func keep(value: Pair[String, float]) -> Pair[String, float]:
		return value


func test() -> void:
	print(PartialChild[float].new())
