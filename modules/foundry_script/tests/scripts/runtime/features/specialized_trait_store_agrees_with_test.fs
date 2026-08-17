# The static relation now answers the trait question the runtime relation already answered, so every
# row below routes through a `Variant` to keep the analyzer out of the way and show what the runtime
# decides on its own. Absent evidence stores and tests `false`; exact evidence stores and tests
# `true`; a subclass and a supertrait projection agree with the direct form. Each case prints the
# store answer and the test answer as an adjacent pair.
trait Keeper[T]:
	func label() -> String:
		return "keeper"


trait Storing[T]:
	uses Keeper[T]


class ForwardingKeeper[U]:
	uses Keeper[U]


class StoringKeeper[U]:
	uses Storing[U]


class StringKeeperChild extends ForwardingKeeper[String]:
	pass


func test() -> void:
	var erased: Variant = ForwardingKeeper.new()
	var erased_slot: Keeper[int] = erased
	print("erased store: ", erased_slot != null)
	print("erased test: ", erased is Keeper[int])

	var exact: Variant = ForwardingKeeper[int].new()
	var exact_slot: Keeper[int] = exact
	print("exact store: ", exact_slot != null)
	print("exact test: ", exact is Keeper[int])

	var subclass: Variant = StringKeeperChild.new()
	var subclass_slot: Keeper[String] = subclass
	print("subclass store: ", subclass_slot != null)
	print("subclass test: ", subclass is Keeper[String])

	var supertrait: Variant = StoringKeeper[int].new()
	var supertrait_slot: Keeper[int] = supertrait
	print("supertrait store: ", supertrait_slot != null)
	print("supertrait test: ", supertrait is Keeper[int])

	var raw: Variant = ForwardingKeeper[String].new()
	var raw_slot: Keeper = raw
	print("raw store: ", raw_slot != null)
	print("raw test: ", raw is Keeper)
