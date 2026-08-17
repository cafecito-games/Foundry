# A specialized return type enforces its arguments on the returned value, under the same gradual rule
# the stores use: an unspecialized value passes, and only a value whose reified arguments contradict
# the declaration is rejected.
class Pair[A, B]:
	pass


class Source:
	func give(value: Variant) -> Pair[int, String]:
		return value


func test() -> void:
	var source := Source.new()
	print(source.give(Pair[int, String].new()) != null)
	print(source.give(Pair.new()) != null)
	print(source.give(Pair[int, Node].new()) != null)
