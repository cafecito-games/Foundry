trait Holder[T]:
	pass


class Box[T] uses Holder[T]:
	pass


func want(_handle: Type[Holder[int]]) -> void:
	print("not ok")


func test() -> void:
	var erased: Variant = Box[String]
	want(erased)
