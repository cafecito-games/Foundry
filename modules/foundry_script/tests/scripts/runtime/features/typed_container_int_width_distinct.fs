# `int` now carries the same 32-bit descriptor as every other public integer spelling, so a container
# typed with it enforces that width at runtime insertion, the same way `Array[uint]` already did, while
# a sibling `Array[long]`/`Dictionary` value slot keeps accepting the same value untouched.
func test():
	var source: Variant = 3000000000

	var ints: Array[int] = [0]
	var longs: Array[long] = [0]
	longs[0] = source
	print(longs)

	var lookup: Dictionary[String, long] = { "value": 0 }
	lookup["value"] = source
	print(lookup)

	ints[0] = source
	print(ints)
