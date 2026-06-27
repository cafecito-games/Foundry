# godot.reflection exposes a read-only introspection surface: method and
# property descriptors plus trait conformance, for a script type or instance.
trait Drawable:
	abstract func draw_self() -> void

trait Sprite uses Drawable:
	var label: String
	var count: int
	abstract func render() -> void
	func describe() -> String:
		return "sprite"

trait Unrelated:
	abstract func z() -> void

func test() -> void:
	var method_names: Array = []
	for method in godot.reflection.get_methods(Sprite):
		method_names.append(str(method["name"]))
	print("render" in method_names)
	print("describe" in method_names)

	var property_names: Array = []
	for property in godot.reflection.get_properties(Sprite):
		property_names.append(str(property["name"]))
	print("label" in property_names)
	print("count" in property_names)

	print(godot.reflection.implements_trait(Sprite, Drawable))
	print(godot.reflection.implements_trait(Sprite, Sprite))
	print(godot.reflection.implements_trait(Sprite, Unrelated))

	print(godot.reflection.get_method_info(Sprite, "render")["name"])
