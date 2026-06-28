func accept[T](_klass: Type[T], _value: T) -> void:
	pass


func test():
	var klass: Variant = Node
	var resource := Resource.new()
	accept(klass, resource)
	print("not ok")
