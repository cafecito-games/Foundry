class Box[T]:
	pass


class StaticScriptHolder:
	static var value: GDScript


func print_erased_static_value() -> void:
	print(StaticScriptHolder.value is GDScript)
	print(godot.reflection.get_type_arguments(StaticScriptHolder.value.new()).size())


func test() -> void:
	StaticScriptHolder.value = Box[int]
	print_erased_static_value()

	var dynamic_static_holder: Variant = StaticScriptHolder
	dynamic_static_holder.value = Box[int]
	print_erased_static_value()

	print("specialized Type handle static widening ok")
