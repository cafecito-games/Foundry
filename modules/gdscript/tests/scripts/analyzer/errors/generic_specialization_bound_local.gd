# The class type-parameter bound is enforced at every specialization site, including a local
# variable annotation inside a function (the scenario described in the issue), not only on class
# members. `int` does not satisfy the `RefCounted` upper bound of `T`.
class Holder[T: RefCounted]:
	var value: T


func test() -> void:
	var h: Holder[int]
	print(h)
