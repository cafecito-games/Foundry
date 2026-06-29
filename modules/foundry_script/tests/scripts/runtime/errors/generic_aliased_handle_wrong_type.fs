# Member-type validation against the reified argument is identical for an instance built through a
# stored handle: a wrong-typed write reaching the `T`-typed member through an untyped path is
# rejected at runtime, just as for the direct `Box[int].new()` form.
class Box[T]:
	var value: T

	func put(v) -> void:
		value = v


const IntBox = Box[int]


func test() -> void:
	var box := IntBox.new()
	box.put("not an int")
	print(box.value)
