class Box[T]:
	var klass: Type[T]


func test():
	var box := Box[Resource].new()
	var dynamic: Variant = box
	var resource := Resource.new()
	var dynamic_resource: Variant = resource
	dynamic.klass = dynamic_resource
	print("not ok")
