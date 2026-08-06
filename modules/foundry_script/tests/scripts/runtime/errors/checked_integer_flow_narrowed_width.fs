# `value` is untyped, so its declared address type is dynamic; the analyzer narrows it to `uint`
# inside the `is uint` branch. Code generation must check the addition at that narrowed width, not
# at the widest range the underlying `uint` carrier can hold, or this addition prints an out-of-range
# result instead of overflowing.
func widen(value):
	if value is uint:
		print(value + 4294967295U)

func test():
	widen(2U)
