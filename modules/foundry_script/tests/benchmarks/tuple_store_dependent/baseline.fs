# Baseline side: the same observable work as feature.fs -- one generic-receiver method, one loop,
# one store per iteration of the same already-canonical value -- into an untyped slot, so the
# difference measured is exactly the receiver-dependent typed tuple store's projection, decode,
# allocation, and checking work.
extends RefCounted


class Crate[T]:
	@warning_ignore_start("unused_variable")
	func store_loop(iterations: int, source: Variant) -> void:
		for index in iterations:
			var stored = source


func supply(value) -> Variant:
	return value


func run_benchmark(iterations: int) -> void:
	var crate := Crate[String].new()
	var source: Variant = supply((1, "one"))
	crate.store_loop(iterations, source)
