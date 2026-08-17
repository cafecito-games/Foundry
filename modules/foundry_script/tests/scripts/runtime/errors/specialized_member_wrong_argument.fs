# A member typed as a specialized class enforces its arguments on a dynamic write. The value is a
# `Pair` either way, so the nominal answer alone would accept it; the reified arguments are what make
# `Pair[int, Node]` a different type from the declared `Pair[int, String]`.
class Pair[A, B]:
	pass


class Holder:
	var pair: Pair[int, String]


func supply(value: Variant) -> Variant:
	return value


func test() -> void:
	var holder: Variant = Holder.new()
	holder.pair = supply(Pair[int, String].new())
	print("correct specialization stored")
	holder.pair = supply(Pair[int, Node].new())
	print("unreachable")
