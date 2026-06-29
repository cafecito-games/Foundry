# Generic inheritance flows type arguments through the hierarchy. A child can rebind the
# parent's type parameter to its own (`Stack[U] extends List[U]`) or bind it to a concrete
# type (`IntList extends List[int]`). Inherited member access — including method signatures —
# substitutes along the chain.
class List[T]:
	var head: T

	func get_head() -> T:
		return head


# Rebind: `List`'s `T` becomes `Stack`'s `U`, so the inherited `head` is typed `U` here.
class Stack[U] extends List[U]:
	func peek() -> U:
		return head


# Concrete specialization: `List`'s `T` is fixed to `int`, so `head` and `get_head()` are `int`.
class IntList extends List[int]:
	func total() -> int:
		return head


func test() -> void:
	var numbers := IntList.new()
	numbers.head = 7
	# The inherited `get_head()` returns `int` after substitution, so it binds to a typed int.
	var first: int = numbers.get_head()
	print(first)
	print(numbers.total())

	# A matching specialization upcasts: `IntList` is a `List[int]`, and the inherited `head` stays `int`.
	var as_base: List[int] = numbers
	print(as_base.head)

	print("generic inheritance ok")
