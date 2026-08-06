# A conformance whose target is a Foundry Script class answers a specialized target for the class
# itself and for its subclasses, which reach the conformance through the base chain.
class Widget:
	var kept: int = 0


class Fancy extends Widget:
	pass


extend Widget uses GenericStore[int]:
	func store(item: int) -> void:
		kept = item

	func fetch() -> int:
		return kept


func test() -> void:
	var widget: Variant = Widget.new()
	var fancy: Variant = Fancy.new()

	print("widget is GenericStore: ", widget is GenericStore)
	print("widget is GenericStore[int]: ", widget is GenericStore[int])
	print("widget is GenericStore[String]: ", widget is GenericStore[String])

	print("fancy is GenericStore: ", fancy is GenericStore)
	print("fancy is GenericStore[int]: ", fancy is GenericStore[int])
	print("fancy is GenericStore[String]: ", fancy is GenericStore[String])

	var exact_cast: Variant = fancy as GenericStore[int]
	print("exact cast keeps identity: ", exact_cast == fancy)
	var mismatched_cast: Variant = fancy as GenericStore[String]
	print("mismatched cast is null: ", mismatched_cast == null)
