class Box[T]:
	pass


class Derived[T] extends Box[Array[T]]:
	pass


func test() -> void:
	var erased: Variant = Derived[int]
	var target: Type[Box[Array[int]]] = erased
	print(target != null)
	print("type metatype erased dependent projection ok")
