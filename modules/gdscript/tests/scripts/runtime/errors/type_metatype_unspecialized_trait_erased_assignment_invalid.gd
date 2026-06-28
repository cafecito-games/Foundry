trait Holder[T]:
	pass


class Box[T] uses Holder[T]:
	pass


var stored_int_holder: Type[Holder[int]]


func test() -> void:
	var erased: Variant = Box
	stored_int_holder = erased
	print("not ok")
