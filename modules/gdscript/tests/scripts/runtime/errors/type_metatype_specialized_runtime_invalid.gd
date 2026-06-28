class Box[T]:
	pass


var stored_int_box: Type[Box[int]]

func test() -> void:
	stored_int_box = Box[String]
	print("not ok")
