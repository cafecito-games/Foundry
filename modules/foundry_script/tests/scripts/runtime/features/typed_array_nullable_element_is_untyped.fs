# A nullable element type is spelled, but the language gives `Array[uint?]` no typed container: it
# reports `is_typed() == false`, so the element store enforces nothing and admits a value the slot's
# declared type cannot hold. This fixture records that behaviour so the defect is represented rather
# than described: https://github.com/cafecito-games/Foundry/issues/2515 changes what it prints.
class Slots extends RefCounted:
	var values: Array[uint?] = [null]

func supply(value):
	return value

func test() -> void:
	var slots := Slots.new()
	print("typed " + str(slots.values.is_typed()))
	slots.values[0] = supply(RefCounted.new())
	var stored: Variant = slots.values[0]
	print("admitted " + str(stored is RefCounted))
