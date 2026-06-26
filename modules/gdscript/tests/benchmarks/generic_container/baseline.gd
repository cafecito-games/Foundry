# Baseline side: the same container shape and observable work as feature.gd, but
# specialized to `int` with a vanilla `Array[int]` member instead of `Array[T]`.
# The only difference from feature.gd is the absence of the type parameter.
extends RefCounted

class IntBox:
	var items: Array[int] = []
	func add(item: int) -> void:
		items.append(item)
	func size() -> int:
		return items.size()
	func reset() -> void:
		items.clear()

func run_benchmark(iterations: int) -> void:
	var box := IntBox.new()
	for index in iterations:
		box.add(index)
		if box.size() > 16:
			box.reset()
