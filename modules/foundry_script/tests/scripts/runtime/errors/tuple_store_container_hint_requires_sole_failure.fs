# The clause that blames a single untyped container element only appears when that element is the sole
# failure. Here it is not: a tuple test rejects null in an element whose declaration is not nullable, so
# the object element fails as well and the message stays in its plain form rather than naming the
# container as the only thing wrong.
func keep(value) -> Variant:
	var kept: (Node, Array[int]) = value
	return kept


func supply(value: Variant) -> Variant:
	return value


func test() -> void:
	print("kept: ", keep(supply((null, [2]))))
