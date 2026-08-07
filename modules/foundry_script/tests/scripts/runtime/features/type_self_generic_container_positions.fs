# Container positions holding `Self` on a generic declaring class keep their element metadata, so a
# member and a container return type reify against the receiver's leaf rather than collapsing to
# untyped containers just because the declaring class has type parameters.
class Base[T]:
	var siblings: Array[Self] = []
	var by_name: Dictionary[String, Self] = {}

	func collect() -> Array[Self]:
		return [self]


class Child extends Base[int]:
	pass


func test() -> void:
	var child := Child.new()
	print("member array is Child: %s" % [child.siblings.get_typed_script() == Child])
	print("member dictionary value is Child: %s" % [child.by_name.get_typed_value_script() == Child])
	print("return array is Child: %s" % [child.collect().get_typed_script() == Child])
