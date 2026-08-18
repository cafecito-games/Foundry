# A nested generic base is bare in exactly the same way as a top-level one.
class Outer:
	class Box[T]:
		var value: T


class Bad extends Outer.Box:
	pass


func test() -> void:
	print("unreachable")
