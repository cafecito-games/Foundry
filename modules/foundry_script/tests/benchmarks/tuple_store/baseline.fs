# Baseline side: the same observable work as feature.fs -- one loop, one store per iteration of the
# same already-canonical value -- into an untyped slot, so the difference measured is exactly the
# typed tuple store's checking work.
extends RefCounted


func supply(value) -> Variant:
	return value


@warning_ignore_start("unused_variable")
func run_benchmark(iterations: int) -> void:
	var source: Variant = supply((1, "one"))
	for index in iterations:
		var stored = source
