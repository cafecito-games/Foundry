# A static member declared on a generic base is never copied into a subclass (unlike instance
# members), so a write reached through the subclass still finds the member on the base. Its OPEN
# binding indexes the base's own type parameter, so the binding must be projected through the
# subclass's `extends Base[int]` specialization rather than treated as unresolvable: a dynamic write
# of a `String` is rejected at runtime whether reached through the subclass itself or through an
# instance of the subclass.
class Box[T]:
	static var value: T


class IntBox extends Box[int]:
	pass


func test() -> void:
	var dynamic_class: Variant = IntBox
	dynamic_class.value = 5
	print(IntBox.value)
	dynamic_class.value = "not an int"
	print(IntBox.value)
