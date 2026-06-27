func accept[T](_klass: Type[T], value: T) -> T:
	return value


func test():
	var klass: Type[Node] = Node
	var node := Node.new()
	var inferred: Node = accept(klass, node)
	print(inferred == node)
	node.free()
