# The `as!` bit-reinterpret operator, exercised at run time. Each operand comes from a typed local so
# the value flows through the reinterpret opcode rather than being folded at compile time. The same
# file runs from source and from exported bytecode, so both must produce these exact numbers.
func test():
	# int -> uint at 32 bits: -1 keeps only its low 32 bits, so it lands as 0xFFFFFFFF, not the
	# all-ones 64-bit pattern.
	var minus_one: int = -1
	print(minus_one as! uint)

	# uint -> int at 32 bits: the all-ones 32-bit pattern is signed -1 once it lands on the int carrier.
	var all_ones_32: uint = 4294967295U
	print(all_ones_32 as! int)

	# ulong -> long at 64 bits: the pattern survives untouched, only the carrier changes.
	var high_byte: ulong = 0xFF00000000000000UL
	print(high_byte as! long)

	# long -> ulong at 64 bits: -1 reads back as the all-ones 64-bit unsigned value.
	var signed_neg: long = -1L
	print(signed_neg as! ulong)

	# The unsigned-shift-then-reinterpret pattern: assemble a value whose top byte is set using an
	# unsigned shift (a signed shift would overflow), then reinterpret the pattern as signed.
	var base: ulong = 255UL
	var shift_count: ulong = 56UL
	print((base << shift_count) as! long)
