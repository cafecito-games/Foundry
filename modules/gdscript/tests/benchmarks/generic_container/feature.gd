# Feature side: a generic container `Box[T]` instantiated at a concrete type. The
# member `Array[T]` and the `T`-typed method parameter exercise the fork's type
# argument reification. Observable work matches baseline.gd exactly; the only
# difference is the type parameter `[T]` vs the concrete `int`.
extends RefCounted

class Box[T]:
	var items: Array[T] = []
	func add(item: T) -> void:
		items.append(item)
	func size() -> int:
		return items.size()
	func reset() -> void:
		items.clear()

func run_benchmark(iterations: int) -> void:
	var box := Box[int].new()
	for index in iterations:
		box.add(index)
		if box.size() > 16:
			box.reset()
