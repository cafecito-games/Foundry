# Generic inheritance flows type arguments through the hierarchy. A child can rebind the
# parent's type parameter to its own (`Stack[U] extends List[U]`) or bind it to a concrete
# type (`IntList extends List[int]`). Inherited member access substitutes along the chain.
class List[T]:
	var head: T


# Rebind: `List`'s `T` becomes `Stack`'s `U`, so the inherited `head` is typed `U` here.
class Stack[U] extends List[U]:
	func peek() -> U:
		return head


# Concrete specialization: `List`'s `T` is fixed to `int`, so `head` is `int`.
class IntList extends List[int]:
	func total() -> int:
		return head


func test() -> void:
	var numbers := IntList.new()
	numbers.head = 7
	print(numbers.total())

	print("generic inheritance ok")
