# An instance-typed argument does not satisfy a handle-typed bound either: the two layers are
# compared, not silently unified.
class Holder[T: Type[Node]]:
	var value: T


func test():
	print(Holder[Node].new())
