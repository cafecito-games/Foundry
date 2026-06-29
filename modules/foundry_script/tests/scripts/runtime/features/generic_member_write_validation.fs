# Writes to a `T`-typed member are checked at runtime against the type argument
# reified onto the instance, while well-typed values (including ones that convert)
# are accepted.
class Box[T]:
	var value: T

	func _init(initial = null):
		value = initial


func test():
	var int_box := Box[int].new(1)
	var dynamic_int: Variant = int_box
	dynamic_int.value = 42
	print(int_box.value)
	dynamic_int.value = 7.0
	print(int_box.value)

	var string_box := Box[String].new("a")
	var dynamic_string: Variant = string_box
	dynamic_string.value = "hello"
	print(string_box.value)
