# A member slot names a nested specialized element in full. The binding validator renders the slot
# through the same renderer the top-level type uses, so an element of `Array[Box[int]]` keeps its
# argument instead of collapsing to `Array[Box]` and understating what the value has to satisfy.
class Box[A]:
	var held


class Base[X]:
	var value: X

	func store(v) -> void:
		value = v


class ArrayHolder extends Base[Array[Box[int]]]:
	pass


class HandleHolder extends Base[Array[Type[Box[int]]]]:
	pass


class DictionaryHolder extends Base[Dictionary[String, Box[int]]]:
	pass


func test() -> void:
	ArrayHolder.new().store(1)
	HandleHolder.new().store(1)
	DictionaryHolder.new().store(1)
	print("nested element diagnostics rendered")
