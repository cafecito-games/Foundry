# Regression guard for the forms that already carry their narrowed or declared width correctly and
# must keep doing so once a flow-narrowed type test starts contributing its own width: a nullable
# declaration narrowed by a null guard, an `Array[uint]` element read, and an in-range result from a
# value narrowed by an `is uint` type test.
func add_after_null_guard(value: uint?):
	if value == null:
		return null
	return value + 1U

func read_array_element(values: Array[uint], index: int) -> uint:
	return values[index]

func widen(value):
	if value is uint:
		return value + 1U
	return null

func test():
	print(add_after_null_guard(4294967294U))
	print(read_array_element([1U, 2U, 4294967295U], 2))
	print(widen(4294967294U))
