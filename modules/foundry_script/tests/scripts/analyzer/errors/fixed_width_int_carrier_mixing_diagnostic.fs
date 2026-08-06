# A variable `uint` mixed with a variable `long` promotes to `long` without a diagnostic (design section
# 6.1: `uint -> long` is value-preserving, see `fixed_width_integer_promotions.fs`). A folded constant
# pair does not go through that promotion -- it is checked at its own carrier instead -- so a suffixed
# `uint`/`long` constant pair still reaches the carrier-mixing diagnostic and its `long` conversion
# advice, in both operand orders. An unsuffixed constant has no pinned width of its own (design section
# 6.1's separate representable-constant crossing), so this needs an explicit suffix on both operands.
func test():
	print(1U + 1L)
	print(1L + 1U)
