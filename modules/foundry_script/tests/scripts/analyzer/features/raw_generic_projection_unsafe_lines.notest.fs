class Box[T]:
	var stored: T

	func get_value() -> T:
		return stored

	func size() -> int:
		return 1


func assign_from_raw(box: Box) -> void:
	var local: int = box.get_value()
	print(local)


func assign_from_specialized(box: Box[int]) -> void:
	var local: int = box.get_value()
	print(local)


func return_from_raw(box: Box) -> int:
	return box.get_value()


func return_from_specialized(box: Box[int]) -> int:
	return box.get_value()


func return_independent_member(box: Box) -> int:
	return box.size()
