class Base:
	func take_items(_items: Array[Self]) -> void:
		pass

	func take_mapping(_mapping: Dictionary[String, Self]) -> void:
		pass

	func check() -> void:
		take_items([self])
		take_mapping({ "item": self })


class Child:
	extends Base


func test() -> void:
	Base.new().check()
	Child.new().check()
