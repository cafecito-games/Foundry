# FoundryScript reflection keeps the exact declared type of a producer compiled from a separate file,
# while the generic `PropertyInfo`/`MethodInfo` half of the same descriptor still carries only the
# `INT`/`UINT` carrier. `Array[int]` and `Array[long]` therefore stay distinguishable even though both
# encode as `INT`, and the exact names come from the compiled descriptors rather than from any value.
#
# A scalar `int` slot now names itself `int`: it carries the same 32-bit constraint its typed-container
# form already did, which is why `Array[int]` names its element exactly too. A method type parameter is
# erased before execution, so a generic signature names `Variant`.
const Producer = preload("fixed_width_integer_reflection_producer.notest.fs")


func test() -> void:
	for property in foundry.reflection.get_property_descriptors(Producer):
		print(property.name, " ", property.type_name, " carrier=", property.type, " hint_string=", property.hint_string)

	var method_names: Array[StringName] = [&"measure", &"measure_unsigned", &"echo", &"inherited_only",
			&"overridable", &"collect", &"nullable_slot"]
	for method_name in method_names:
		var descriptor := foundry.reflection.get_method_descriptor(Producer, method_name)
		var carriers: Array = []
		for argument in descriptor.args:
			carriers.append(argument["type"])
		# The exact names stay index-parallel with the generic argument list, including for a method
		# with a rest parameter and a default argument.
		print(descriptor.name, " ", descriptor.arg_type_names, " -> ", descriptor.return_type_name,
				" carriers=", carriers, " -> ", descriptor.return_value["type"],
				" parallel=", descriptor.arg_type_names.size() == descriptor.args.size())

	# The bulk enumeration carries the same names, including for the inherited declarations.
	var enumerated: Array = []
	for descriptor in foundry.reflection.get_method_descriptors(Producer):
		enumerated.append(str(descriptor.name, ":", descriptor.return_type_name))
	enumerated.sort()
	print(enumerated)

	# The loosely-keyed Dictionary view carries the same exact names.
	var info: Dictionary = foundry.reflection.get_method_info(Producer, "measure")
	print(info["arg_type_names"], " -> ", info["return_type_name"])
	var property_info: Dictionary = foundry.reflection.get_properties(Producer)[5]
	print(property_info["name"], " ", property_info["type_name"])
