# The same subsumption holds for the alternatives of a bounded type parameter: `Number` cannot be
# split into five disjoint arms, because `uint` and `ulong` share the unsigned carrier just as `int`
# and `long` share the signed one.
func widest_first[X: Number](value: X) -> String:
	if value is ulong:
		return "ulong"
	elif value is uint:
		return "uint"
	return "other"
