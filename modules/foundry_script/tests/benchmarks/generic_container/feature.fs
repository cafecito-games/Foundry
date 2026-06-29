# Feature side: a generic container `Box[T]` instantiated at a concrete type,
# exercising the fork's generic-class machinery (specialized handle reification,
# generic dispatch). Observable work matches baseline.gd: both append an int per
# iteration and clear past a bound. The baseline mirrors this with a plain class
# over an untyped array so the only measured difference is the generic class.
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
