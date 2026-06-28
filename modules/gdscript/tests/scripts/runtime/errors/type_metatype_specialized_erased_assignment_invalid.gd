class Box[T]:
	pass


var stored_int_box: Type[Box[int]]


func test() -> void:
	var erased: Variant = Box[String]
	stored_int_box = erased
	print("not ok")
