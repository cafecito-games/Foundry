# Feature side: writes target a `T`-typed member of a generic class. Each store
# is validated at runtime against the reified type argument (`int` here). The
# `store` argument is left untyped so the only difference from baseline.gd is the
# member type: a reified-`T` validated write vs a plain `int` write. (A `T`-typed
# argument would add a separate arg-conversion asymmetry that is not the point of
# this case.)
extends RefCounted

class Holder[T]:
	var value: T
	func store(item) -> void:
		value = item

func run_benchmark(iterations: int) -> void:
	var holder := Holder[int].new()
	for index in iterations:
		holder.store(index)
