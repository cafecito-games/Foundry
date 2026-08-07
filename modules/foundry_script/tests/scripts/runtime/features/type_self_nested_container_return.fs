# A `Self` position nested inside another container is typed against the receiver exactly like a
# single-level one, so nested literals returned from `-> Array[Array[Self]]` and
# `-> Array[Dictionary[String, Self]]` analyze cleanly and reify against the leaf.
class Base:
	func grid() -> Array[Array[Self]]:
		return [[self]]

	func rows() -> Array[Dictionary[String, Self]]:
		return [{ "item": self }]


class Child extends Base:
	pass


func test() -> void:
	var child := Child.new()
	var grid := child.grid()
	print("inner array is Child typed: %s" % [grid[0].get_typed_script() == Child])
	print("nested element is Child: %s" % [grid[0][0] is Child])
	var rows := child.rows()
	print("nested dictionary is Child typed: %s" % [rows[0].get_typed_value_script() == Child])
	print("nested dictionary value is Child: %s" % [rows[0]["item"] is Child])
