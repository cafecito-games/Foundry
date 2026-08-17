# The projection reads the conformer's whole chain, so every shape that carries a contradicting
# argument is rejected: a subclass of a specialized conformer, a conformer reaching the trait through
# a supertrait, a conformer that fixes the argument in its own `uses`, and a trait-typed source.
trait Keeper[T]:
	func label() -> String:
		return "keeper"


trait Storing[T]:
	uses Keeper[T]


class ForwardingKeeper[U]:
	uses Keeper[U]


class StringKeeperChild extends ForwardingKeeper[String]:
	pass


class StoringKeeper[U]:
	uses Storing[U]


class IntKeeper:
	uses Keeper[int]


func test() -> void:
	var subclass_slot: Keeper[int] = StringKeeperChild.new()
	print(subclass_slot)
	var supertrait_slot: Keeper[int] = StoringKeeper[String].new()
	print(supertrait_slot)
	var fixed_slot: Keeper[String] = IntKeeper.new()
	print(fixed_slot)
	var wide: Keeper[String] = ForwardingKeeper[String].new()
	var narrow: Keeper[int] = wide
	print(narrow)
