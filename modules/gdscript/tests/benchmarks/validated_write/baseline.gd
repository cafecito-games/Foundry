# Baseline side: the same observable work as feature.gd, but the member is a
# concrete `int`. The `store` argument is untyped to match feature.gd, so the
# only difference is the member type: a plain `int` write here vs the feature's
# reified-`T` validated write.
extends RefCounted

class Holder:
	var value: int
	func store(item) -> void:
		value = item

func run_benchmark(iterations: int) -> void:
	var holder := Holder.new()
	for index in iterations:
		holder.store(index)
