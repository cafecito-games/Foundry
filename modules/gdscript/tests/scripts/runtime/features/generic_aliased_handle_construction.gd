# A specialized generic class handle stored in a `const` alias or a local `var` carries its reified
# type arguments through to `.new()`, binding the same arguments as the direct `Box[int].new()` form
# (erasure-on-widening: preserved while the static type still names them). Reflection, member-type
# validation, and `create_proxy[T]` behave identically to the direct form.
namespace cafecito.specialized_handle

annotation marker(value: String = "") targets CLASS, METHOD

trait Greeter:
	abstract func greet(subject: String) -> String


@marker("box")
class Box[T]:
	var value: T

	func _handle(_method_name: StringName, _args: Array) -> Variant:
		return "stub"

	func make_proxy() -> T:
		return create_proxy[T](_handle)

	@marker("describe")
	static func describe() -> String:
		return "static ok"


const IntBox = Box[int]
const GreeterBox = Box[Greeter]
const WidenedConst: GDScript = Box[int]
const VariantConst: Variant = Box[int]


func return_widened() -> GDScript:
	return Box[int]


func return_script() -> Script:
	return Box[int]


func return_resource() -> Resource:
	return Box[int]


func return_object() -> Object:
	return Box[int]


func accept_gdscript(script: GDScript) -> int:
	return godot.reflection.get_type_arguments(script.new()).size()


func accept_script(script: Script) -> int:
	return godot.reflection.get_type_arguments(script.call("new")).size()


func accept_resource(resource: Resource) -> bool:
	return resource is GDScript


func accept_ref_counted(ref_counted: RefCounted) -> bool:
	return ref_counted is GDScript


func accept_object(object: Object) -> bool:
	return object is GDScript


func test() -> void:
	# A `const` alias binds the same reified argument as the direct form.
	var from_const := IntBox.new()
	print(godot.reflection.get_type_arguments(from_const)[0]["type"] == TYPE_INT)
	from_const.value = 7 # validated and stored against the reified `int`
	print(from_const.value)
	var erased_const_handle: Variant = IntBox
	var typed_from_erased_const: Type[Box[int]] = erased_const_handle
	print(typed_from_erased_const == Box[int])

	# A local with a hard inferred specialized type.
	var hard_handle := Box[int]
	var from_hard := hard_handle.new()
	print(godot.reflection.get_type_arguments(from_hard)[0]["type"] == TYPE_INT)

	# An untyped local handle still names the arguments through its inferred type.
	var weak_handle = Box[int]
	var from_weak = weak_handle.new()
	print(godot.reflection.get_type_arguments(from_weak)[0]["type"] == TYPE_INT)

	# `create_proxy[T]` resolves `T` through an instance built from a const alias.
	var greeter_box := GreeterBox.new()
	var greeter = greeter_box.make_proxy()
	print(greeter != null and greeter is Greeter)

	# Erasure-on-widening: a handle widened to `GDScript` no longer names the argument, so the
	# constructed instance carries none (mirrors `Array[int]` widening to `Array`).
	var widened: GDScript = Box[int]
	print(godot.reflection.get_type_arguments(widened.new()).size())

	var widened_script: Script = Box[int]
	print(widened_script is GDScript)
	print(godot.reflection.get_type_arguments(widened_script.call("new")).size())

	var widened_resource: Resource = Box[int]
	print(widened_resource is GDScript)

	var widened_object: Object = Box[int]
	print(widened_object is GDScript)

	print(return_widened() is GDScript)
	print(return_script() is GDScript)
	print(godot.reflection.get_type_arguments(return_script().call("new")).size())
	print(return_resource() is GDScript)
	print(return_object() is GDScript)
	print(WidenedConst is GDScript)
	print(accept_gdscript(Box[int]))
	print(accept_script(Box[int]))
	print(accept_resource(Box[int]))
	print(accept_ref_counted(Box[int]))
	print(accept_object(Box[int]))
	var erased_call_handle: Variant = Box[int]
	@warning_ignore("unsafe_call_argument")
	print(accept_gdscript(erased_call_handle))
	@warning_ignore("unsafe_call_argument")
	print(accept_script(erased_call_handle))
	@warning_ignore("unsafe_call_argument")
	print(accept_resource(erased_call_handle))
	@warning_ignore("unsafe_call_argument")
	print(accept_ref_counted(erased_call_handle))
	@warning_ignore("unsafe_call_argument")
	print(accept_object(erased_call_handle))
	var typed_from_variant_const: Type[Box[int]] = VariantConst
	print(typed_from_variant_const == Box[int])

	print(Box[int].describe())
	var erased_int_handle: Variant = Box[int]
	var erased_int_handle_again: Variant = Box[int]
	var erased_string_handle: Variant = Box[String]
	print(erased_int_handle is GDScript)
	@warning_ignore("unsafe_cast")
	var cast_int_handle := erased_int_handle as GDScript
	print(cast_int_handle != null)
	print(godot.reflection.get_type_arguments(cast_int_handle.new()).size())
	print(godot.reflection.get_method_info(erased_int_handle, &"describe").has("name"))
	print(godot.reflection.get_class_annotations(erased_int_handle)[0].args[0])
	print(godot.reflection.get_method_annotations(erased_int_handle, &"describe")[0].args[0])
	print(erased_int_handle == erased_int_handle_again)
	print(erased_int_handle == erased_string_handle)
	var specialized_lookup := { erased_int_handle: "int box" }
	print(specialized_lookup[erased_int_handle_again])

	const LocalIntBox = Box[int]
	var erased_local_const: Variant = LocalIntBox
	var typed_from_erased_local_const: Type[Box[int]] = erased_local_const
	print(typed_from_erased_local_const == Box[int])

	print("aliased handle construction ok")
