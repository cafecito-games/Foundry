# Baseline side: plain vanilla FoundryScript doing the same observable work as
# feature.fs, but with a direct method call on a concretely typed local instead
# of a trait-typed dispatch. The only difference from feature.fs is the trait.
extends RefCounted

class Greeter:
	func greet(value: int) -> int:
		return value + 1

func run_benchmark(iterations: int) -> void:
	var greeter := Greeter.new()
	var accumulator: int = 0
	for index in iterations:
		accumulator = greeter.greet(accumulator)
