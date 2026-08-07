# A rest tail typed `Array[Self]` on a generic declaring class reifies its element type against the
# invoked receiver, exactly like an equivalent declared `Array[Self]` parameter does. The declaring
# class being generic must not give the same declaration a different runtime shape.
class Base[T]:
	static func take(...values: Array[Self]) -> void:
		print("generic rest is Child: %s" % [values.get_typed_script() == Child])

	static func take_items(items: Array[Self]) -> void:
		print("generic declared is Child: %s" % [items.get_typed_script() == Child])


class Child extends Base[int]:
	pass


class Plain:
	static func take(...values: Array[Self]) -> void:
		print("plain rest is PlainChild: %s" % [values.get_typed_script() == PlainChild])

	static func take_items(items: Array[Self]) -> void:
		print("plain declared is PlainChild: %s" % [items.get_typed_script() == PlainChild])


class PlainChild extends Plain:
	pass


func test() -> void:
	var handle: Variant = Child
	handle.take(Child.new())
	var items: Array[Child] = [Child.new()]
	handle.take_items(items)
	var plain: Variant = PlainChild
	plain.take(PlainChild.new())
	var plain_items: Array[PlainChild] = [PlainChild.new()]
	plain.take_items(plain_items)
