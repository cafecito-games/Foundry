# Specializing a generic base in the `extends` clause must supply the right argument count.
class List[T]:
	var head: T


class Bad extends List[int, String]:
	pass


func test() -> void:
	print("unreachable")
