const External = preload("type_metatype_specialized_handle_external.notest.gd")


class Box[T]:
	var value: T


class Pair[A, B] extends Box[A]:
	var other: B


var stored_int_box: Type[Box[int]]


func accept_int_box_type(klass: Type[Box[int]]) -> Box[int]:
	return klass.new()


func return_int_box_type() -> Type[Box[int]]:
	return Box[int]


func return_pair_as_int_box_type() -> Type[Box[int]]:
	return Pair[int, String]


func print_int_argument(instance: Box[int]) -> void:
	print(godot.reflection.get_type_arguments(instance)[0]["type"] == TYPE_INT)


func test() -> void:
	var direct := Box[int].new()
	print_int_argument(direct)

	var local_int_box: Type[Box[int]] = Box[int]
	var from_local := local_int_box.new()
	print_int_argument(from_local)
	from_local.value = 17
	print(from_local.value)

	stored_int_box = Box[int]
	var from_stored := stored_int_box.new()
	print_int_argument(from_stored)

	var from_returned := return_int_box_type().new()
	print_int_argument(from_returned)

	var from_argument := accept_int_box_type(Box[int])
	print_int_argument(from_argument)

	var from_widened_pair: Variant = return_pair_as_int_box_type().new()
	print(from_widened_pair is Pair)
	print(godot.reflection.get_type_arguments(from_widened_pair).size())
	from_widened_pair.other = "ok"
	print(from_widened_pair.other)

	var external_int_box = External.Box[int]
	var from_external = external_int_box.new()
	print(godot.reflection.get_type_arguments(from_external)[0]["type"] == TYPE_INT)

	print("specialized Type handle construction ok")
