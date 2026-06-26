# Baseline side: the same observable work as feature.gd, but the computation runs
# through a direct method call on a concrete object instead of a dynamic proxy.
# The only difference from feature.gd is the proxy dispatch path.
extends RefCounted

class Concrete:
	func compute(value: int) -> int:
		return value * 2 - value

func run_benchmark(iterations: int) -> void:
	var concrete := Concrete.new()
	var accumulator: int = 0
	for index in iterations:
		accumulator += concrete.compute(index) - accumulator
