# A named tuple's tuple-typed field types the literal written for it, the same way its container
# fields already did.
tuple Bag(pair: (int, Array[int]), label: String)


func test():
	var bag := Bag((1, []), "x")
	print(bag)
	print(bag.pair[1].get_typed_builtin() == TYPE_INT)
