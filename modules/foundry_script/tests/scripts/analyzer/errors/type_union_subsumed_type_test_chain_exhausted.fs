# Every alternative here shares the signed carrier, and `long` holds every `int` value, so a failed
# `is long` rules out the whole set. Subtraction cannot express an empty result -- there is no bottom
# type to narrow to -- so the later `is int` would still be checked against the full set and pass.
# The exhausting test is what is wrong, so it is what is reported: nothing can reach its false
# branch, and the arms after it are dead.
type Small = int | long


func widest_first(value: Small) -> String:
	if value is long:
		return "long"
	elif value is int:
		return "int"
	return "unreachable"


func test():
	print(widest_first(5))
