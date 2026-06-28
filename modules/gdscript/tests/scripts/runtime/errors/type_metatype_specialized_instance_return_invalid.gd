class Box[T]:
	pass


func get_box() -> Box:
	var erased: Variant = Box[int]
	return erased


func test() -> void:
	print(get_box())
