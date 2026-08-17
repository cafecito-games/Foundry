# A `Variant` element supplies no typing, and it does not stop the sibling element from being typed.
func test():
	var pair: (Variant, Array[int]) = ("a", [])
	print(pair)
	print(pair[1].get_typed_builtin() == TYPE_INT)
