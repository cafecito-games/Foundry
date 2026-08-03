func test():
	var i: int = 1
	var u: uint = 2U
	var l: long = 3L
	var ul: ulong = 4UL

	# No integer type holds every value of both operands, in either order.
	var long_ulong: ulong = l + ul
	var ulong_long: ulong = ul + l
	var int_ulong: ulong = i + ul
	var ulong_int: ulong = ul + i

	# A dynamic value may not narrow or cross signedness.
	var narrowed: uint = ul
	var crossed: uint = l
	var lossy_float: float = l

	# A constant is only accepted when its exact value fits.
	var too_large: uint = 5000000000UL
