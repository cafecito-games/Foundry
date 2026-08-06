# `int` now carries the same 32-bit width every other public integer spelling does, so mixing it with
# `uint` reaches the carrier-mixing diagnostic and its `long` conversion advice instead of the generic
# operand message, in both operand orders.
func test():
	var i: int = 1
	var u: uint = 1U
	print(i + u)
	print(u + i)
