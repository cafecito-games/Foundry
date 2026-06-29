class Base:
	func call_items() -> void:
		take_items([self])
		take_mapping({ "item": self })

	func take_items(_items: Array[Self]) -> void:
		pass

	func take_mapping(_mapping: Dictionary[String, Self]) -> void:
		pass


class Child:
	extends Base

	func take_items(items: Array[Child]) -> void:
		print(items[0] is Child)

	func take_mapping(mapping: Dictionary[String, Child]) -> void:
		print(mapping["item"] is Child)


func test() -> void:
	var base: Base = Child.new()
	base.call_items()
