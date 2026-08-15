# `Number` is compiler-provided and wins a type lookup outright, so a declaration that reuses the
# name could never be reached from a type position. It is reported at the declaration rather than
# left silently dead.
type Number = int


class Inner:
	class Number:
		pass


func test():
	print("unreachable")
