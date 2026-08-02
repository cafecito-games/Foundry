# A generic slot annotated `Array[Type[T]]` erases its element metadata exactly like `Array[T]`, so the
# declared parameter type carries no runtime descriptor. Enforcement still comes from the value that was
# passed in, which keeps its own class-handle descriptor, so the erased annotation is not a loophole.
func collect[T](handles: Array[Type[T]]) -> int:
	var untyped: Array = handles
	untyped.append(RefCounted.new())
	return handles.size()


func test():
	var nodes: Array[Type[Node]] = [Node]
	print(collect(nodes))
