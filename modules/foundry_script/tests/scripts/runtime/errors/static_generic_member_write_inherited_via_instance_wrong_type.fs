# A static member declared on a generic base and reached through an instance of a subclass has its
# own instance `type_arguments` set (or empty, since the subclass fixed the argument via `extends`),
# but the member itself was never copied into the subclass: it stays on the base with an OPEN binding
# indexed against the base's own type parameter. That binding must be projected through the leaf
# instance's specialization chain, not resolved against `type_arguments` directly, so a dynamic write
# of a `String` into an `IntBox` instance's inherited static slot is rejected at runtime.
class Box[T]:
	static var value: T


class IntBox extends Box[int]:
	pass


func test() -> void:
	var box := IntBox.new()
	var dynamic_instance: Variant = box
	dynamic_instance.value = 5
	print(box.value)
	dynamic_instance.value = "not an int"
	print(box.value)
