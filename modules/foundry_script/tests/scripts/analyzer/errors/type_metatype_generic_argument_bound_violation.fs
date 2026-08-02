# A class handle denotes a class, not values of it, so it does not satisfy an instance-typed bound.
# Accepting it would let the generic body call `Node` instance methods on an `FSNativeClass` value.
class Holder[T: Node]:
	var value: T

	func label() -> String:
		return value.get_name()


func test():
	var holder := Holder[Type[Node]].new()
	print(holder.label())
