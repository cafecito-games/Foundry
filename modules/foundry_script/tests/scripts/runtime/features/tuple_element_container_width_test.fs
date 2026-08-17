# A typed-container element never reaches the scalar width path: the structural test compares the
# container's declared element type as a whole, which already carries the width. So `Array[int]` and
# `Array[long]` stay distinguishable as tuple elements with no per-element width rule of their own,
# and the contents keep being enforced by container validation on write.
func supply(value: Variant) -> Variant:
	return value


func test():
	var ints: Array[int] = [1, 2]
	var longs: Array[long] = [9223372036854775807]

	var slot: (int, Array[int]) = supply([7, ints])
	print(slot)

	var narrow: Variant = supply([7, ints])
	print(narrow is (int, Array[int]))
	print(narrow is (int, Array[long]))

	var wide: Variant = supply([7, longs])
	print(wide is (int, Array[int]))
	print(wide is (int, Array[long]))
