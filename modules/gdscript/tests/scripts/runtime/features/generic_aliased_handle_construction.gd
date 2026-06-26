# A specialized generic class handle stored in a `const` alias or a local `var` carries its reified
# type arguments through to `.new()`, binding the same arguments as the direct `Box[int].new()` form
# (erasure-on-widening: preserved while the static type still names them). Reflection, member-type
# validation, and `create_proxy[T]` behave identically to the direct form.
trait Greeter:
	@abstract func greet(subject: String) -> String


class Box[T]:
	var value: T

	func _handle(_method_name: StringName, _args: Array) -> Variant:
		return "stub"

	func make_proxy() -> T:
		return create_proxy[T](_handle)


const IntBox = Box[int]
const GreeterBox = Box[Greeter]


func test() -> void:
	# A `const` alias binds the same reified argument as the direct form.
	var from_const := IntBox.new()
	print(godot.reflection.get_type_arguments(from_const)[0]["type"] == TYPE_INT)
	from_const.value = 7 # validated and stored against the reified `int`
	print(from_const.value)

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

	print("aliased handle construction ok")
