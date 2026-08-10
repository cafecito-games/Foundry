# Methods declared against a class or trait type parameter compile once, so top-level Array and
# Dictionary return metadata is erased at runtime. Every concrete consumer must retype the value from
# the unspecialized declaration while gradual consumers must keep the erased value.
class Parcel[T]:
	pass


class Bag[T]:
	func singleton(value: T) -> Array[T]:
		var result: Array[T] = []
		result.append(value)
		return result

	func mapping(value: T) -> Dictionary[String, T]:
		var result: Dictionary[String, T] = {}
		result["item"] = value
		return result

	func nested(value: T) -> Array[Array[T]]:
		return [[value]]

	func parcels() -> Array[Parcel[T]]:
		return []


class Derived extends Bag[int]:
	pass


trait ContainerTrait[T]:
	func trait_values(value: T) -> Array[T]:
		var result: Array[T] = []
		result.append(value)
		return result

	func trait_mapping(value: T) -> Dictionary[String, T]:
		return { "item": value }

	func trait_nested(value: T) -> Array[Array[T]]:
		return [[value]]


trait ForwardTrait[U] uses ContainerTrait[U]:
	pass


trait RelayTrait[V] uses ForwardTrait[V]:
	pass


class DirectTraitBag uses ContainerTrait[int]:
	pass


class ForwardingBag[U] uses ContainerTrait[U]:
	pass


class TransitiveBag[U] uses RelayTrait[U]:
	pass


class Holder:
	var values: Array[int]
	var mapping: Dictionary[String, int]
	var property_values: Array[int]:
		set(value):
			property_values = value


var field_values: Array[int] = Bag[int].new().singleton(1)
var field_mapping: Dictionary[String, int] = Bag[int].new().mapping(2)
static var static_values: Array[int] = Bag[int].new().singleton(3)
static var static_mapping: Dictionary[String, int] = Bag[int].new().mapping(4)


func accept_values(values: Array[int]) -> void:
	Utils.check(values.get_typed_builtin() == TYPE_INT)
	Utils.check(values == [45])


func returned_values() -> Array[int]:
	return Bag[int].new().singleton(46)


func test() -> void:
	var bag := Bag[int].new()

	var direct: Array[int] = bag.singleton(41)
	Utils.check(direct.get_typed_builtin() == TYPE_INT)
	Utils.check(direct == [41])

	var inferred := bag.singleton(42)
	Utils.check(inferred.get_typed_builtin() == TYPE_INT)
	Utils.check(inferred == [42])

	var assigned: Array[int]
	assigned = bag.singleton(43)
	Utils.check(assigned.get_typed_builtin() == TYPE_INT)
	Utils.check(assigned == [43])

	var mapping: Dictionary[String, int] = bag.mapping(44)
	Utils.check(mapping.get_typed_key_builtin() == TYPE_STRING)
	Utils.check(mapping.get_typed_value_builtin() == TYPE_INT)
	Utils.check(mapping == { "item": 44 })
	var derived := Derived.new()
	var inherited_values: Array[int] = derived.singleton(45)
	var inherited_mapping: Dictionary[String, int] = derived.mapping(46)
	Utils.check(inherited_values.get_typed_builtin() == TYPE_INT)
	Utils.check(inherited_values == [45])
	Utils.check(inherited_mapping.get_typed_key_builtin() == TYPE_STRING)
	Utils.check(inherited_mapping.get_typed_value_builtin() == TYPE_INT)
	Utils.check(inherited_mapping == { "item": 46 })

	accept_values(bag.singleton(45))
	var returned := returned_values()
	Utils.check(returned.get_typed_builtin() == TYPE_INT)
	Utils.check(returned == [46])

	var nested: Array[Array[int]] = bag.nested(47)
	Utils.check(nested.get_typed_builtin() == TYPE_ARRAY)
	Utils.check(nested[0].get_typed_builtin() == TYPE_INT)
	Utils.check(nested == [[47]])
	var parcels: Array[Parcel[int]] = bag.parcels()
	Utils.check(parcels.get_typed_script() == Parcel)
	Utils.check(parcels.is_empty())

	var holder := Holder.new()
	holder.values = bag.singleton(49)
	holder.mapping = bag.mapping(50)
	holder.property_values = bag.singleton(51)
	Utils.check(holder.values.get_typed_builtin() == TYPE_INT)
	Utils.check(holder.mapping.get_typed_value_builtin() == TYPE_INT)
	Utils.check(holder.property_values.get_typed_builtin() == TYPE_INT)

	Utils.check(field_values.get_typed_builtin() == TYPE_INT)
	Utils.check(field_mapping.get_typed_value_builtin() == TYPE_INT)
	Utils.check(static_values.get_typed_builtin() == TYPE_INT)
	Utils.check(static_mapping.get_typed_value_builtin() == TYPE_INT)

	var class_callback := bag.singleton
	var called: Array[int] = class_callback.call(52)
	var callved: Array[int] = class_callback.callv([53])
	Utils.check(called.get_typed_builtin() == TYPE_INT)
	Utils.check(callved.get_typed_builtin() == TYPE_INT)

	var mapping_callback := bag.mapping
	var called_mapping: Dictionary[String, int] = mapping_callback.call(54)
	var callved_mapping: Dictionary[String, int] = mapping_callback.callv([55])
	Utils.check(called_mapping.get_typed_value_builtin() == TYPE_INT)
	Utils.check(callved_mapping.get_typed_value_builtin() == TYPE_INT)
	var constructed_values := Callable(bag, "singleton")
	var constructed_call: Array[int] = constructed_values.call(66)
	var constructed_callv: Array[int] = constructed_values.callv([67])
	Utils.check(constructed_call.get_typed_builtin() == TYPE_INT)
	Utils.check(constructed_callv.get_typed_builtin() == TYPE_INT)
	var created_mapping := Callable.create(bag, &"mapping")
	var created_call: Dictionary[String, int] = created_mapping.call(68)
	var created_callv: Dictionary[String, int] = created_mapping.callv([69])
	Utils.check(created_call.get_typed_value_builtin() == TYPE_INT)
	Utils.check(created_callv.get_typed_value_builtin() == TYPE_INT)
	var inherited_callable := Callable(derived, "singleton")
	var inherited_called: Array[int] = inherited_callable.call(70)
	Utils.check(inherited_called.get_typed_builtin() == TYPE_INT)

	var direct_trait := DirectTraitBag.new()
	var direct_trait_values: Array[int] = direct_trait.trait_values(56)
	var direct_trait_mapping: Dictionary[String, int] = direct_trait.trait_mapping(57)
	Utils.check(direct_trait_values.get_typed_builtin() == TYPE_INT)
	Utils.check(direct_trait_mapping.get_typed_value_builtin() == TYPE_INT)

	var forwarding := ForwardingBag[int].new()
	var forwarding_values: Array[int] = forwarding.trait_values(58)
	var forwarding_nested: Array[Array[int]] = forwarding.trait_nested(59)
	Utils.check(forwarding_values.get_typed_builtin() == TYPE_INT)
	Utils.check(forwarding_nested.get_typed_builtin() == TYPE_ARRAY)

	var transitive := TransitiveBag[int].new()
	var transitive_values: Array[int] = transitive.trait_values(60)
	var transitive_mapping: Dictionary[String, int] = transitive.trait_mapping(61)
	Utils.check(transitive_values.get_typed_builtin() == TYPE_INT)
	Utils.check(transitive_mapping.get_typed_value_builtin() == TYPE_INT)

	var trait_callback := transitive.trait_values
	var trait_called: Array[int] = trait_callback.call(62)
	var trait_callved: Array[int] = trait_callback.callv([63])
	Utils.check(trait_called.get_typed_builtin() == TYPE_INT)
	Utils.check(trait_callved.get_typed_builtin() == TYPE_INT)
	var trait_callable := Callable.create(transitive, &"trait_mapping")
	var trait_callable_result: Dictionary[String, int] = trait_callable.callv([71])
	Utils.check(trait_callable_result.get_typed_value_builtin() == TYPE_INT)

	var untyped_values: Array = bag.singleton(64)
	var untyped_mapping: Dictionary = bag.mapping(65)
	Utils.check(untyped_values.get_typed_builtin() == TYPE_NIL)
	Utils.check(untyped_mapping.get_typed_key_builtin() == TYPE_NIL)
	Utils.check(untyped_mapping.get_typed_value_builtin() == TYPE_NIL)

	print("class parameter typed container return ok")
