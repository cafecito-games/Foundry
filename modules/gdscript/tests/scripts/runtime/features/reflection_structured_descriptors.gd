# godot.reflection exposes structured method and property descriptors as typed
# GDScriptMethodDescriptor / GDScriptPropertyDescriptor objects, giving typed access to a
# member's metadata (name, arguments, return value, flags) plus its passive annotations,
# alongside the back-compatible loosely-keyed Dictionary form via to_dictionary().
namespace cafecito.descriptors

annotation marker targets METHOD
annotation tag(value: String) targets VARIABLE

class Base:
	@tag("hp")
	var health: int = 10

	@marker
	func attack(target: String, power: int = 5) -> bool:
		return power >= 0 and target != ""

class Derived extends Base:
	func defend() -> void:
		pass

func test() -> void:
	# Structured method descriptors carry typed metadata and embedded annotations.
	var by_name := {}
	for descriptor: GDScriptMethodDescriptor in godot.reflection.get_method_descriptors(Base):
		by_name[str(descriptor.name)] = descriptor
	var attack: GDScriptMethodDescriptor = by_name["attack"]
	print(attack.name)
	print(attack.args.size())
	print(attack.args[0]["name"])
	print(attack.args[1]["name"])
	print(attack.return_value["type"] == TYPE_BOOL)
	print(attack.default_args)
	print(attack.flags == attack.get_flags())
	print(attack.annotations.size())
	print(attack.annotations[0].name)

	# get_method_descriptor resolves an inherited method through the derived script.
	var inherited := godot.reflection.get_method_descriptor(Derived, "attack")
	print(inherited.name)
	print(inherited.annotations.size())
	print(godot.reflection.get_method_descriptor(Base, "missing") == null)

	# to_dictionary mirrors the loosely-keyed get_method_info, annotations included.
	var info := godot.reflection.get_method_info(Base, "attack")
	print(attack.to_dictionary()["name"] == info["name"])
	var embedded: Array = attack.to_dictionary()["annotations"]
	print(embedded.size())

	# Structured property descriptors carry typed metadata and embedded annotations.
	for descriptor: GDScriptPropertyDescriptor in godot.reflection.get_property_descriptors(Base):
		if str(descriptor.name) == "health":
			print(descriptor.type == TYPE_INT)
			print(descriptor.usage == descriptor.get_property_usage())
			print(descriptor.annotations.size())
			print(descriptor.annotations[0].name)
			print(descriptor.to_dictionary()["name"])

	# Invalid / non-script targets stay safe: empty arrays and null descriptors.
	print(godot.reflection.get_method_descriptors(42).size())
	print(godot.reflection.get_method_descriptor(42, "x") == null)
	print(godot.reflection.get_property_descriptors(null).size())
