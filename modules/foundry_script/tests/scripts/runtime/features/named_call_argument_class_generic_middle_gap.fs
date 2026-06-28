# A named call may leave a middle gap on a method whose skipped parameter depends
# on the enclosing class's type parameter. The constant default is inlined, but
# the synthesized value is excluded from the receiver's type-argument validation,
# so it matches a trailing omitted default the callee fills in itself.

class Box[T]:
	func pick(first: T, second: T = 0, count: int = 1) -> T:
		prints(first, second, count)
		return first

func test():
	var box := Box[String].new()
	# `second` depends on the class type parameter `T` (String here). Its int
	# default is inlined without being validated against String.
	prints("got", box.pick("x", count = 2))
	# Trailing-omit baseline: the callee applies the same default for `second`.
	prints("got", box.pick("y"))
