class Box[T]:
	pass


func accept_int_box_type(_klass: Type[Box[int]]) -> void:
	pass


func test() -> void:
	var erased: Variant = Box[String]
	accept_int_box_type(erased)
	print("not ok")
