# Validating the write before the setter does not retire the setter's own write-back check: a setter
# body may store any value in the member, not only the one it was handed, so a value it invents is
# still rejected at the write-back inside the setter.
class Box[T]:
	var value: T:
		set(incoming):
			value = "not an int"

	func put(supplied) -> void:
		value = supplied


func test() -> void:
	var box := Box[int].new()
	box.put(1)
	print(box.value)
