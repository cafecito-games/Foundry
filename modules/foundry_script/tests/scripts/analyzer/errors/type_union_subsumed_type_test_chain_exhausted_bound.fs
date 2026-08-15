# The alternatives of a bounded type parameter are exhausted by the same rule, and the unsigned
# carrier behaves exactly like the signed one: every `uint` value is also a `ulong` value, so a
# failed `is ulong` leaves nothing for the `uint` arm to match.
func widest_first[X: uint | ulong](value: X) -> String:
	if value is ulong:
		return "ulong"
	elif value is uint:
		return "uint"
	return "unreachable"


func test():
	print(widest_first(5U))
