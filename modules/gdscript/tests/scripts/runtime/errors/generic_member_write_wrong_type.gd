# A write to a `T`-typed member is validated at runtime against the instance's
# reified type argument, so storing a `String` into a `Box[int]` fails.
class Box[T]:
	var value: T

	func _init(initial = null):
		value = initial


func test():
	var box := Box[int].new(0)
	var dynamic: Variant = box
	dynamic.value = "not an int"
	print(box.value)
