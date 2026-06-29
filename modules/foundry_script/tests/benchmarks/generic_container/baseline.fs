# Baseline side: the same container shape and observable work as feature.gd, with
# a plain non-generic class. The member is an untyped `Array` and `add` takes an
# untyped argument so the per-iteration element handling matches the feature's:
# the fork erases `Array[T]` to an untyped array at runtime, so a vanilla
# `Array[int]` baseline would pay typed-array element validation the feature
# skips, biasing the comparison. Keeping both untyped leaves the generic-class
# machinery (specialized handle, generic dispatch) as the only measured difference.
extends RefCounted

class IntBox:
	var items: Array = []
	func add(item) -> void:
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
