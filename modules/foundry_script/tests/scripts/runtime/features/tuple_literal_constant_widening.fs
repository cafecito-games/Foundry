# A constant element is widened to the declared element type at construction, exactly as an array
# literal and a named tuple construction already widen theirs. The exact value is known, so this is
# not a conversion at the store.
func test():
	var widened: (float, int) = (1, 2)
	print(widened)
	print(typeof(widened[0]) == TYPE_FLOAT)

	var nested: (int, (float, float)) = (1, (2, 3))
	print(nested)
	print(typeof(nested[1][0]) == TYPE_FLOAT)
