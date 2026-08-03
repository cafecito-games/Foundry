func test():
	# A declared width is part of the literal's type, so a "ulong" never enters a "uint" slot.
	var _value: uint = 18446744073709551615UL
