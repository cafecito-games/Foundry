# `value` is untyped, so its declared address type is dynamic; the analyzer narrows it to `uint`
# inside the `is uint` branch. The compound assignment's implicit read of the current value must be
# checked at that narrowed width, not at the widest range the underlying `uint` carrier can hold, or
# this addition silently wraps instead of overflowing.
func widen(value):
	if value is uint:
		value += 4294967295U

func test():
	widen(1U)
