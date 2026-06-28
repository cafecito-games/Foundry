# godot.reflection accepts typed class handles (Type[T] values) as reflection targets.
# At runtime a Type[T] value is the same class-handle object as the bare class name.
namespace cafecito.type_metatype_reflect

annotation marked targets CLASS, METHOD

@marked
trait Drawable:
	abstract func draw_self() -> void

trait Creatable:
	abstract static func create() -> Self

@marked
class Sprite uses Drawable:
	var label: String

	func draw_self() -> void:
		pass

class User uses Creatable:
	static func create() -> User:
		return User.new()


func _method_names(target: Variant) -> Array:
	var names: Array = []
	for method in godot.reflection.get_methods(target):
		names.append(str(method["name"]))
	names.sort()
	return names


func _property_names(target: Variant) -> Array:
	var names: Array = []
	for property in godot.reflection.get_properties(target):
		names.append(str(property["name"]))
	names.sort()
	return names


func test() -> void:
	var user_type: Type[User] = User
	var sprite_type: Type[Sprite] = Sprite
	var drawable_type: Type[Drawable] = Sprite
	var creatable_trait: Type[Creatable] = Creatable

	print(_method_names(User) == _method_names(user_type))
	print("create" in _method_names(user_type))

	print(_property_names(Sprite) == _property_names(sprite_type))
	print("label" in _property_names(sprite_type))

	print(godot.reflection.implements_trait(sprite_type, Drawable))
	print(godot.reflection.implements_trait(sprite_type, Sprite))
	print(godot.reflection.implements_trait(user_type, Creatable))
	print(godot.reflection.implements_trait(user_type, creatable_trait))
	print(godot.reflection.implements_trait(user_type, Drawable))
	print(godot.reflection.implements_trait(drawable_type, Drawable))

	print(user_type == User)
	print(sprite_type == Sprite)

	print(godot.reflection.get_class_annotations(user_type).size())
	print(godot.reflection.get_class_annotations(sprite_type).size())
	print(godot.reflection.has_annotation(sprite_type, "", "marked", "class"))
	print(godot.reflection.get_method_info(user_type, "create")["name"])
