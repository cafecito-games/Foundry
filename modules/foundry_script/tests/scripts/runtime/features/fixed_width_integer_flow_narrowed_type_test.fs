# A `Variant` narrowed by a type test reaches the operator through the dynamic path, which discovers
# only the wide carriers: `uint`/`uint` is checked as `ulong`. The declared-width sibling is
# fixed_width_integer_flow_narrowed_nullable_overflow.fs, where the parameter's own type carries the
# width and the same addition overflows.
func increment(value: Variant):
	if value is uint:
		return value + 1U
	return null

func test():
	print(increment(4294967295U))
