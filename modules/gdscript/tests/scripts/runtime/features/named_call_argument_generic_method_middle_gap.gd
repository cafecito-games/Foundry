# A named call may leave a middle gap on a generic method even when the skipped
# parameter's declared type depends on the method's type parameter. The constant
# default is inlined at the call site, but the synthesized value is excluded from
# type-parameter inference and post-substitution validation, so it behaves exactly
# like a trailing omitted default the callee fills in itself.

func describe[T](first: T, second: T = 0, count: int = 1) -> void:
	prints(first, second, count)

func test():
	# Inferred T comes from `first` alone; the synthesized `second` default does
	# not constrain T. Equivalent to `describe(1)` with an explicit `count`.
	describe(1, count = 5)
	# Explicit type argument. The int default baked for `second` is not validated
	# against String, mirroring a trailing omitted default.
	describe[String]("a", count = 9)
	# Trailing-omit baseline for comparison: the callee applies the same default.
	describe("a", "b")
	describe[String]("a")
