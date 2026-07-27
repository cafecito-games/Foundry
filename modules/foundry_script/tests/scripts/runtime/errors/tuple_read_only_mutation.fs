# The analyzer rejects element assignment on a hard tuple type. The read-only Array is the runtime
# backstop for a tuple that escaped into a `Variant`, where no static shape is known.
func subtest_element_assignment(escaped: Variant):
	escaped[0] = 99

func subtest_append(escaped: Variant):
	escaped.append(3)

func test():
	var escaped: Variant = (1, 2)
	subtest_element_assignment(escaped)
	subtest_append(escaped)
	print(escaped)
