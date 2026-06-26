# Baseline side: the same observable work as feature.gd, but the member is a
# concrete `int` so the store is a plain unvalidated write. The only difference
# from feature.gd is the absence of the runtime-validated `T`-typed member.
extends RefCounted

class Holder:
	var value: int
	func store(item: int) -> void:
		value = item

func run_benchmark(iterations: int) -> void:
	var holder := Holder.new()
	for index in iterations:
		holder.store(index)
