# A direct internal store of a wrong-typed value into a `T`-typed member is rejected at runtime
# against the instance's reified type argument, just like an external write.
class Box[T]:
	var value: T

	func put(v) -> void:
		value = v


func test() -> void:
	var box := Box[int].new()
	box.put("not an int")
	print(box.value)
