# Baseline side: the same observable work as feature.fs -- one loop, one call returning the same
# already-constructed instance per iteration -- through an untyped return, so the difference measured is
# exactly the specialized script-typed return's decoding and checking work.
extends RefCounted


class Box[T]:
	var value: T


var source: Variant


func supply(value) -> Variant:
	return value


func produce() -> Variant:
	return source


@warning_ignore_start("unused_variable")
func run_benchmark(iterations: int) -> void:
	source = supply(Box[int].new())
	for index in iterations:
		var produced = produce()
