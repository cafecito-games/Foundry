class Inner:
	pass


class Other:
	pass


class Box[T]:
	pass


var stored: Type[Box[Inner]]


func test() -> void:
	var erased: Variant = Box[Other]
	stored = erased
	print("not ok")
