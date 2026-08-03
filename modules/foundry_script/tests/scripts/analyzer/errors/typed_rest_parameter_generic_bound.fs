# A type argument solved from the rest tail is checked against the parameter's bound exactly like
# one solved from a fixed parameter.
func collect_resource[T: Resource](...values: Array[T]) -> Array[T]:
	return values


func test():
	var bad := collect_resource(Node.new())
	print(bad)
