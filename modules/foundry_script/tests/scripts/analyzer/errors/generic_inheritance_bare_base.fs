# An `extends` edge into a generic class must bind every base type parameter: the edge defines the
# inherited member types of the whole subclass, so a bare base would erase them.
class Box[T]:
	var value: T


class Bad extends Box:
	pass


func test() -> void:
	print("unreachable")
