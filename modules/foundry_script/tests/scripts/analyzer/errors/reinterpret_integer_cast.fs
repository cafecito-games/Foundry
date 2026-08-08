func test():
	var wide: long = 1L
	var narrow: uint = 1U

	# Cross-width reinterprets are rejected so a genuine narrowing cannot hide behind `as!`.
	print(wide as! uint)
	print(wide as! int)
	print(narrow as! ulong)

	# A non-integer operand has no equal-width integer pattern to reinterpret.
	print(1.5 as! int)
	print(true as! int)

	# A non-integer target is not one of the source-nameable integer types `as!` allows.
	print(42 as! float)
