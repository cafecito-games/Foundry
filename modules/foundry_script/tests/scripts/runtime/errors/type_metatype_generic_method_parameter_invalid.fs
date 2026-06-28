func accept[T](_klass: Type[T], _value: T) -> void:
	pass


func test():
	var klass: Variant = 1
	var resource := Resource.new()
	accept(klass, resource)
	print("not ok")
