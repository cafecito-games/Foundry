# A type argument must satisfy the upper bound of its type parameter.
class Box[T: RefCounted]:
	var value: T

var bad: Box[Object]


func test():
	print(bad)
