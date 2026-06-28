class Box[T]:
	var klass: Type[T]

	func _init(initial = null):
		klass = initial


func test():
	var box := Box[Node].new(Node)
	print(box.klass == Node)

	var dynamic: Variant = box
	dynamic.klass = Control
	print(box.klass == Control)
