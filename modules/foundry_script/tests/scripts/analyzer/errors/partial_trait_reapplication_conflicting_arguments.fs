# A trait's type arguments are fixed by the first class on an inheritance chain that applies it, and
# a composite binding is compared component by component: `Keeper[Pair[int, V]]` and
# `Keeper[Pair[String, V]]` agree on the parameter they both leave open and contradict each other on
# the component they both fix, so the re-application is rejected.
trait Keeper[T]:
	func keep(value: T) -> T:
		return value


class Pair[A, B]:
	pass


class Base[U]:
	uses Keeper[Pair[int, U]]


class Child[V] extends Base[V]:
	uses Keeper[Pair[String, V]]


func test() -> void:
	print(Child[int].new())
