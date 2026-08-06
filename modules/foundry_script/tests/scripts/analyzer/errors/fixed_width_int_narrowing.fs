# `int` now enforces the 32-bit signed range at assignment, just like `uint`, `long`, and `ulong`
# already did for their own widths: a `long` value outside that range is refused where it targets an
# `int` destination, even though the same value is accepted by a `long` destination.
func test():
	var source: long = 3000000000L
	var accepted: long = source
	var narrowed: int = source
	print(accepted, narrowed)
