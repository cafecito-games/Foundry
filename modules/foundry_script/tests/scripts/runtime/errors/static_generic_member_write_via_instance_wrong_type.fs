# A generic class's own type parameter is reified onto an instance's `type_arguments`, and a
# static member typed by that parameter resolves against the same reification when written through
# an instance (`Box[int].new()`), so a dynamic write of a `String` is rejected at runtime.
class Box[T]:
	static var value: T


func test() -> void:
	var box := Box[int].new()
	box.value = 5
	print(box.value)

	var dynamic: Variant = box
	dynamic.value = "not an int"
	print(box.value)
