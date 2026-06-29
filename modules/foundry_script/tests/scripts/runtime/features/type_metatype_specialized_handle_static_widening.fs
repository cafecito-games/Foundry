class Box[T]:
	pass


class StaticScriptHolder:
	static var value: FoundryScript


func print_erased_static_value() -> void:
	print(StaticScriptHolder.value is FoundryScript)
	print(foundry.reflection.get_type_arguments(StaticScriptHolder.value.new()).size())


func test() -> void:
	StaticScriptHolder.value = Box[int]
	print_erased_static_value()

	var dynamic_static_holder: Variant = StaticScriptHolder
	dynamic_static_holder.value = Box[int]
	print_erased_static_value()

	print("specialized Type handle static widening ok")
