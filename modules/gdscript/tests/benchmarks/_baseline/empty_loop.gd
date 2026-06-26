# Measures pure harness overhead: the loop and the per-iteration call cost with
# no feature work inside. Every other case's number is read net of this floor.
extends RefCounted

func run_benchmark(iterations: int) -> void:
	var accumulator: int = 0
	for index in iterations:
		accumulator += index
