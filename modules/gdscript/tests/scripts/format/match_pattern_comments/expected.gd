func test():
	match x:
		[
			1,
			# full-line comment between elements
			2,  # inline after element
		]:
			print("array")
		{
			"a": 1,
			# dict gap comment
			"b": 2,
		}:
			print("dict")
		_:
			pass
