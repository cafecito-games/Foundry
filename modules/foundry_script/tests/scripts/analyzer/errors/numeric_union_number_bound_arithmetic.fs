# An operator on a set-typed value is checked for every combination the set allows. `Number` allows
# `long` with `ulong`, which has no common integer type, so two `Number`-bounded values can never be
# added directly: they have to be narrowed or converted first.
func add[X: Number, Y: Number](left: X, right: Y) -> long:
	return left + right


func test():
	print(add(1, 2))
