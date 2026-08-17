# Feature side: every iteration stores into a slot declared `(int, String)`. The slot's declared shape
# is receiver-independent -- it names neither a type parameter nor `Self` -- so the store's cost is the
# structural check plus whatever the shape itself costs to obtain. The source is handed over untyped so
# the analyzer cannot fold the store away, and the value is already a canonical tuple carrier so the
# store's normalization allocates nothing and the measured difference is the checking work.
extends RefCounted


func supply(value) -> Variant:
	return value


@warning_ignore_start("unused_variable")
func run_benchmark(iterations: int) -> void:
	var source: Variant = supply((1, "one"))
	for index in iterations:
		var stored: (int, String) = source
