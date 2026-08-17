# A tuple member is a tuple slot: both write paths -- an in-body store and a reflective `set()` --
# validate the declared shape and store the canonical read-only, untyped carrier, so a member never
# holds an Array its writer can still mutate.
class Holder extends RefCounted:
	var field: (int, String) = (0, "zero")

	func store(value: Variant) -> void:
		field = value

	# Reaching a tuple's carrier takes a deliberate unsafe cast, since a tuple type is not an Array
	# type to the analyzer.
	func report() -> void:
		var value: Variant = field
		var carrier := value as Array
		print("read_only=", carrier.is_read_only(), " typed=", carrier.is_typed())


func supply(value: Variant) -> Variant:
	return value


func test() -> void:
	var holder := Holder.new()
	holder.store(supply((1, "one")))
	print(holder.field)
	holder.report()

	# A mutable Array of the right shape is accepted and normalized rather than stored as handed over.
	holder.store(supply([2, "two"]))
	print(holder.field)
	holder.report()

	var object: Object = holder
	object.set("field", (3, "three"))
	print(holder.field)
	holder.report()
