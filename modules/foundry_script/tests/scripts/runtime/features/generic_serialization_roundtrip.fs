# A reified generic instance (`Box[int].new()`) persists its bound type arguments through resource
# duplication, which travels the same get_property_list/get/set storage path as `.tres`/scene
# save-load. After the round-trip the reified arguments are intact, so `T`-typed member writes are
# validated exactly as on a freshly constructed instance, and an unspecialized instance round-trips
# unchanged.
class Box[T]:
	extends Resource
	var value: T

	func _init(initial = null):
		if initial != null:
			value = initial


func test() -> void:
	var int_box := Box[int].new()
	var clone := int_box.duplicate() as Box

	# The reified `int` argument survived the round-trip.
	var args: Array = godot.reflection.get_type_arguments(clone)
	print(args.size())
	print(args[0]["type"] == TYPE_INT)

	# That argument still governs member writes: a float is coerced to the bound `int`, matching a
	# freshly constructed `Box[int]`.
	var dynamic: Variant = clone
	dynamic.value = 3.5
	print(clone.value)

	# A nested specialized argument (`Box[Array[int]]`) keeps its element metadata.
	var nested := Box[Array[int]].new()
	var nested_clone := nested.duplicate() as Box
	var nested_args: Array = godot.reflection.get_type_arguments(nested_clone)
	print(nested_args.size())
	print(nested_args[0]["type"] == TYPE_ARRAY)

	# An unspecialized instance carries no type arguments; its untyped slot accepts the float as-is.
	var raw := Box.new()
	var raw_clone := raw.duplicate() as Box
	print(godot.reflection.get_type_arguments(raw_clone).size())
	var raw_dynamic: Variant = raw_clone
	raw_dynamic.value = 3.5
	print(raw_clone.value)
