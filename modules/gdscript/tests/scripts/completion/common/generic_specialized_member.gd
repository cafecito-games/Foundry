# Member completion on a specialized generic value lists the class's members.
class Box[T]:
	var value: T
	func get_value() -> T:
		return value

func test() -> void:
	var b := Box[int].new()
	b.➡
