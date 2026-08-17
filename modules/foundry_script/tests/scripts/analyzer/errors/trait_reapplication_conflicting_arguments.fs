# A trait's type arguments are fixed by the first class on an inheritance chain that applies it. A
# subclass that re-applies it with different arguments would make a statically valid call through the
# base type reach a body typed for the other arguments, so the re-application is rejected.
trait Keeper[T]:
	func keep(value: T) -> T:
		return value


class BaseKeeper:
	uses Keeper[String]


class ChildKeeper extends BaseKeeper:
	uses Keeper[int]


class ForwardingKeeper[U]:
	uses Keeper[U]


class SpecializedChild extends ForwardingKeeper[String]:
	uses Keeper[int]


func test() -> void:
	print(ChildKeeper.new())
	print(SpecializedChild.new())
