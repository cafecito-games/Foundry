# `int` with `uint` has a common type, but only because code generation widens the `uint` operand into
# the shared carrier before the operation runs, which needs both operand carriers statically. A
# multi-member union erases to untyped, so the combination has no executable result here.
func test():
	var left: int | long = 2
	var right: uint = 3U
	print(left + right)
