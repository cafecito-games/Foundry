func test():
	var i: int = 1
	var u: uint = 2U
	var l: long = 3L
	var ul: ulong = 4UL

	# No integer type holds every value of both operands, in either order. The two rows that name the
	# legacy `int` spelling still report the generic operand diagnostic instead of the carrier-crossing
	# one, because `int` declares no width yet.
	var long_ulong: ulong = l + ul
	var ulong_long: ulong = ul + l
	var int_ulong: ulong = i + ul
	var ulong_int: ulong = ul + i
	var int_uint: long = i + u
	var uint_int: long = u + i
	var uint_long: long = u + l
	var long_uint: long = l + u

	# A dynamic value may not narrow or cross signedness.
	var narrowed: uint = ul
	var crossed: uint = l
	var lossy_float: float = l
	var lossy_unsigned_float: float = ul

	# The unsigned narrow width does not reach the floating side implicitly today, even though every
	# uint value is exactly representable as a float.
	var unsigned_narrow_float: float = u

	# A constant is only accepted when its exact value fits.
	var too_large: uint = 5000000000UL
