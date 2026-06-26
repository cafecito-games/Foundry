func pick[T](first: T, second: T = 0, count: int = 1) -> T:
	return first if count > 0 else second

func test():
	# `second` has a constant default, but inlining it on a generic function would unify and
	# validate the default against the substituted type parameter, so it must be passed explicitly.
	print(pick(1, count = 2))
