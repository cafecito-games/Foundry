# Baseline side: the same observable work as feature.fs -- one loop, one store per iteration of the
# same already-constructed instance -- into an untyped slot, so the difference measured is exactly the
# specialized script-typed store's decoding and checking work.
extends RefCounted


class Box[T]:
	var value: T


func supply(value) -> Variant:
	return value


@warning_ignore_start("unused_variable")
func run_benchmark(iterations: int) -> void:
	var source: Variant = supply(Box[int].new())
	for index in iterations:
		var stored = source
