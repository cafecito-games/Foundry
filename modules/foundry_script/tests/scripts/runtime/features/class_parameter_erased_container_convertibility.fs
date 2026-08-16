# The check for a slot *declared* as a container of a class type parameter establishes that the value
# CAN become the receiver's specialization, not that it already is it: validation builds a converted
# copy and these slots then store the erased original instead. The concrete consumer's retyping store
# is what makes the contents concrete, so the same call observed gradually and concretely yields
# different element types on purpose. Contents no `Bag[int]` could hold at all still fail inside the
# callee -- see runtime/errors/class_parameter_typed_container_return_wrong_type.fs.
class Bag[T]:
	func from_untyped(value: Variant) -> Array[T]:
		var source: Array = [value]
		return source

	func mapping_from_untyped(value: Variant) -> Dictionary[String, T]:
		var source: Dictionary = { "item": value }
		return source


func test() -> void:
	var bag := Bag[int].new()

	# 1.5 converts to int, so the return's check passes -- and the stored array keeps the float.
	var gradual: Array = bag.from_untyped(1.5)
	Utils.check(gradual.get_typed_builtin() == TYPE_NIL)
	Utils.check(typeof(gradual[0]) == TYPE_FLOAT)
	Utils.check(gradual == [1.5])

	# The same call at a concrete consumer is retyped there, which is where the conversion happens.
	var concrete: Array[int] = bag.from_untyped(1.5)
	Utils.check(concrete.get_typed_builtin() == TYPE_INT)
	Utils.check(typeof(concrete[0]) == TYPE_INT)
	Utils.check(concrete == [1])

	var gradual_mapping: Dictionary = bag.mapping_from_untyped(2.5)
	Utils.check(gradual_mapping.get_typed_value_builtin() == TYPE_NIL)
	Utils.check(typeof(gradual_mapping["item"]) == TYPE_FLOAT)

	var concrete_mapping: Dictionary[String, int] = bag.mapping_from_untyped(2.5)
	Utils.check(concrete_mapping.get_typed_value_builtin() == TYPE_INT)
	Utils.check(concrete_mapping == { "item": 2 })

	print("erased container convertibility ok")
