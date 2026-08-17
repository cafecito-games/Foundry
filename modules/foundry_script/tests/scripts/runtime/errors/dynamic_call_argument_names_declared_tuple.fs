# A rejected call argument is described by the parameter's declaration, not by the carrier the value
# travels in. A tuple erases to an Array, so a carrier-only message would blame `Array` for a shape
# mismatch and leave the reader with nothing to compare against.
func take(pair: (int, String)) -> void:
	print("took ", pair)


func test() -> void:
	var callback: Callable = take
	callback.call((1, "one"))
	callback.call((1, 2))
	print("unreachable")
