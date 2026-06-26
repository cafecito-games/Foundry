# Feature side: writes target a `T`-typed member of a generic class. Each store
# is validated at runtime against the reified type argument (`int` here). The
# observable work matches baseline.gd; the only difference is the validated
# `T`-typed write vs a plain `int` write.
extends RefCounted

class Holder[T]:
	var value: T
	func store(item: T) -> void:
		value = item

func run_benchmark(iterations: int) -> void:
	var holder := Holder[int].new()
	for index in iterations:
		holder.store(index)
