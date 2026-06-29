const External = preload("type_metatype_specialized_handle_external.notest.fs")


class Box[T]:
	const SPECIALIZED_CONST = "box constant"
	static var static_value := "box static"
	var value: T

	class Inner:
		var marker := "inner class"

	static func static_text() -> String:
		return "box static method"


class Pair[A, B] extends Box[A]:
	var other: B


class IntBox extends Box[int]:
	pass


class Holder[T]:
	var value: T

	func assign_box_handle() -> void:
		value = Box[int]


class ScriptHolder:
	var value: FoundryScript


var stored_int_box: Type[Box[int]]


func accept_int_box_type(klass: Type[Box[int]]) -> Box[int]:
	return klass.new()


func return_int_box_type() -> Type[Box[int]]:
	return Box[int]


func return_pair_as_int_box_type() -> Type[Box[int]]:
	return Pair[int, String]


func return_int_box_subclass_type() -> Type[Box[int]]:
	return IntBox


func print_int_argument(instance: Box[int]) -> void:
	print(foundry.reflection.get_type_arguments(instance)[0]["type"] == TYPE_INT)


func test() -> void:
	var direct := Box[int].new()
	print_int_argument(direct)

	var local_int_box: Type[Box[int]] = Box[int]
	var from_local := local_int_box.new()
	print_int_argument(from_local)
	from_local.value = 17
	print(from_local.value)

	var local_int_subclass_box: Type[Box[int]] = IntBox
	var from_subclass_local := local_int_subclass_box.new()
	print(from_subclass_local is IntBox)
	from_subclass_local.value = 23
	print(from_subclass_local.value)

	stored_int_box = Box[int]
	var from_stored := stored_int_box.new()
	print_int_argument(from_stored)

	var from_returned := return_int_box_type().new()
	print_int_argument(from_returned)

	var from_subclass_returned := return_int_box_subclass_type().new()
	print(from_subclass_returned is IntBox)

	var from_argument := accept_int_box_type(Box[int])
	print_int_argument(from_argument)

	var from_subclass_argument := accept_int_box_type(IntBox)
	print(from_subclass_argument is IntBox)

	var from_widened_pair: Variant = return_pair_as_int_box_type().new()
	print(from_widened_pair is Pair)
	print(foundry.reflection.get_type_arguments(from_widened_pair).size())
	print(foundry.reflection.get_type_arguments(from_widened_pair)[1]["type"] == TYPE_STRING)
	from_widened_pair.other = "ok"
	print(from_widened_pair.other)

	var external_int_box = External.Box[int]
	var from_external = external_int_box.new()
	print(foundry.reflection.get_type_arguments(from_external)[0]["type"] == TYPE_INT)

	var dynamic_int_box_handle: Variant = Box[int]
	@warning_ignore("unsafe_property_access")
	print(dynamic_int_box_handle.SPECIALIZED_CONST)
	@warning_ignore("unsafe_property_access")
	print(dynamic_int_box_handle.static_value)
	@warning_ignore("unsafe_property_access")
	var dynamic_inner_class: Variant = dynamic_int_box_handle.Inner
	print(dynamic_inner_class is FoundryScript)
	@warning_ignore("unsafe_method_access")
	var dynamic_inner_instance: Variant = dynamic_inner_class.new()
	@warning_ignore("unsafe_property_access")
	print(dynamic_inner_instance.marker)
	@warning_ignore("unsafe_property_access")
	var dynamic_static_callable: Callable = dynamic_int_box_handle.static_text
	print(dynamic_static_callable.call())

	var external_property_holder := Holder[FoundryScript].new()
	external_property_holder.value = Box[int]
	print(external_property_holder.value is FoundryScript)
	print(foundry.reflection.get_type_arguments(external_property_holder.value.new()).size())

	var internal_property_holder := Holder[FoundryScript].new()
	internal_property_holder.assign_box_handle()
	print(internal_property_holder.value is FoundryScript)
	print(foundry.reflection.get_type_arguments(internal_property_holder.value.new()).size())

	var regular_property_holder := ScriptHolder.new()
	regular_property_holder.value = Box[int]
	print(regular_property_holder.value is FoundryScript)
	print(foundry.reflection.get_type_arguments(regular_property_holder.value.new()).size())

	var typed_script_array: Array[FoundryScript] = [Box[int]]
	print(typed_script_array[0] is FoundryScript)
	print(foundry.reflection.get_type_arguments(typed_script_array[0].new()).size())

	var appended_script_array: Array[FoundryScript] = []
	appended_script_array.append(Box[int])
	print(appended_script_array[0] is FoundryScript)
	print(foundry.reflection.get_type_arguments(appended_script_array[0].new()).size())

	var set_script_array: Array[FoundryScript] = []
	Utils.check(set_script_array.resize(1) == OK)
	set_script_array.set(0, Box[int])
	print(set_script_array[0] is FoundryScript)
	print(foundry.reflection.get_type_arguments(set_script_array[0].new()).size())

	var indexed_script_array: Array[FoundryScript] = []
	Utils.check(indexed_script_array.resize(1) == OK)
	indexed_script_array[0] = Box[int]
	print(indexed_script_array[0] is FoundryScript)
	print(foundry.reflection.get_type_arguments(indexed_script_array[0].new()).size())

	var typed_script_dictionary: Dictionary[FoundryScript, FoundryScript] = { Box[int]: Box[int] }
	var dictionary_key = typed_script_dictionary.keys()[0]
	var dictionary_value = typed_script_dictionary.values()[0]
	print(dictionary_key is FoundryScript)
	print(foundry.reflection.get_type_arguments(dictionary_key.new()).size())
	print(dictionary_value is FoundryScript)
	print(foundry.reflection.get_type_arguments(dictionary_value.new()).size())

	var set_script_dictionary: Dictionary[FoundryScript, FoundryScript] = {}
	print(set_script_dictionary.set(Box[int], Box[int]))
	var set_dictionary_key = set_script_dictionary.keys()[0]
	var set_dictionary_value = set_script_dictionary.values()[0]
	print(set_dictionary_key is FoundryScript)
	print(foundry.reflection.get_type_arguments(set_dictionary_key.new()).size())
	print(set_dictionary_value is FoundryScript)
	print(foundry.reflection.get_type_arguments(set_dictionary_value.new()).size())

	var indexed_script_dictionary: Dictionary[FoundryScript, FoundryScript] = {}
	var indexed_dictionary_key_arg: Variant = Box[int]
	var indexed_dictionary_value_arg: Variant = Box[int]
	indexed_script_dictionary[indexed_dictionary_key_arg] = indexed_dictionary_value_arg
	var indexed_dictionary_key = indexed_script_dictionary.keys()[0]
	var indexed_dictionary_value = indexed_script_dictionary.values()[0]
	print(indexed_dictionary_key is FoundryScript)
	print(foundry.reflection.get_type_arguments(indexed_dictionary_key.new()).size())
	print(indexed_dictionary_value is FoundryScript)
	print(foundry.reflection.get_type_arguments(indexed_dictionary_value.new()).size())

	var variant_value_dictionary: Dictionary[FoundryScript, Variant] = {}
	var variant_value_key: Variant = Box[int]
	var specialized_value: Variant = Box[int]
	variant_value_dictionary[variant_value_key] = specialized_value
	print(variant_value_dictionary.size() == 1)
	@warning_ignore("unsafe_call_argument")
	print(variant_value_dictionary.find_key(specialized_value) is FoundryScript)

	var nested_script_array: Array[Array[FoundryScript]] = []
	var nested_array_argument: Variant = [Box[int]]
	@warning_ignore("unsafe_call_argument")
	nested_script_array.append(nested_array_argument)
	print(nested_script_array[0][0] is FoundryScript)
	print(foundry.reflection.get_type_arguments(nested_script_array[0][0].new()).size())

	var nested_script_dictionary: Dictionary[String, Array[FoundryScript]] = {}
	var nested_dictionary_value: Variant = [Box[int]]
	@warning_ignore("unsafe_call_argument")
	print(nested_script_dictionary.set("values", nested_dictionary_value))
	print(nested_script_dictionary["values"][0] is FoundryScript)
	print(foundry.reflection.get_type_arguments(nested_script_dictionary["values"][0].new()).size())

	print("specialized Type handle construction ok")
