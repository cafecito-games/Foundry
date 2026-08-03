# The boundary table the virtual machine answers from. Every row is at, or one step inside, the edge
# of a declared width, so a result that silently wrapped or widened would print a different number.
# The same file is executed from source and from exported bytecode, so both must agree here.
func test():
	var unsigned_max: uint = 4294967295U
	var unsigned_long_max: ulong = 18446744073709551615UL
	var signed_max: long = 9223372036854775807L
	var signed_min: long = -9223372036854775808L
	var zero_unsigned: uint = 0U

	print(unsigned_max)
	print(unsigned_long_max)
	print(signed_max)
	print(signed_min)

	# Arithmetic keeps the operands' width instead of promoting to whatever fits.
	print(unsigned_max - 1U)
	print(unsigned_long_max - 1UL)
	print(signed_max - 1L)
	print(signed_min + 1L)
	print(zero_unsigned + unsigned_max)

	# The unsigned carrier survives an assignment, so the sum is stored rather than dropped.
	var unsigned_sum: uint = zero_unsigned + unsigned_max
	print(unsigned_sum)

	# Division and remainder truncate toward zero on both carriers.
	var nine_unsigned: ulong = 9UL
	var two_unsigned: ulong = 2UL
	var negative_nine: long = -9L
	var two_signed: long = 2L
	print(nine_unsigned / two_unsigned)
	print(nine_unsigned % two_unsigned)
	print(negative_nine / two_signed)
	print(negative_nine % two_signed)

	# A shift is checked at the left operand's width, and the count is not a second range.
	var one_unsigned: uint = 1U
	var thirty_one: uint = 31U
	print(one_unsigned << thirty_one)
	print(unsigned_max >> thirty_one)

	# Bitwise complement flips only the bits the declared width has.
	print(~zero_unsigned)
	print(-signed_max)

	# A checked cast converts into the destination's carrier rather than through a constructor that
	# has none for the unsigned carrier.
	var from_signed: long = 42L
	var fractional: float = 3.9
	print(from_signed as uint)
	print(from_signed as ulong)
	print(unsigned_max as long)
	print(fractional as long)
	print(unsigned_long_max as ulong)

	# A width-constrained type test is a range test as well as a carrier test, so a value whose
	# carrier matches but whose magnitude the width cannot hold is not a value of that type.
	var erased: Array = [4294967296UL, 5UL, 42L]
	print(erased[0] is uint)
	print(erased[1] is uint)
	print(erased[0] is ulong)
	print(erased[2] is long)
	print(erased[2] is uint)
