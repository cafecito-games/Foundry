# A `T`-typed field initializer is validated at construction against the instance's reified type
# argument, so a literal that does not match the bound type fails.
class Box[T]:
	var value: T = "always a string"


func test() -> void:
	var box := Box[int].new()
	print(box.value)
