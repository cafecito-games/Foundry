class Box[T]:
	var klass: Type[T]


func test():
	var box := Box[int].new()
	var dynamic: Variant = box
	dynamic.klass = Node
	print("not ok")
