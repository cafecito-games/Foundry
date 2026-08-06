func test():
	var i: int = 1
	var u: uint = 2U
	var l: long = 3L
	var ul: ulong = 4UL

	# No integer type holds every value of both operands, in either order. `int`/`uint` and `uint`/`long`
	# are not here: design section 6.1 promotes both pairs to `long`, since every value on each side is
	# representable there (see the `fixed_width_integer_promotions.fs` feature fixture).
	var long_ulong: ulong = l + ul
	var ulong_long: ulong = ul + l
	var int_ulong: ulong = i + ul
	var ulong_int: ulong = ul + i

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
