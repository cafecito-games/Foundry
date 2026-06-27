# Feature side: dispatch through a trait-typed local. The class adopts the trait
# with `uses`, and the call site holds the value at the trait type so the fork's
# trait dispatch path is exercised. Observable work matches baseline.gd exactly.
extends RefCounted

trait Greeting:
	abstract func greet(value: int) -> int

class Greeter uses Greeting:
	func greet(value: int) -> int:
		return value + 1

func run_benchmark(iterations: int) -> void:
	var greeter: Greeting = Greeter.new()
	var accumulator: int = 0
	for index in iterations:
		accumulator = greeter.greet(accumulator)
