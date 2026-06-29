class Box[T]:
	pass


func get_int_box_type() -> Type[Box[int]]:
	var erased: Variant = Box[String]
	return erased


func test() -> void:
	print(get_int_box_type())
