# Feature side: every iteration stores into a slot declared `(int, T)` on a generic receiver
# specialized as `Crate[String]`. The slot names a type parameter, so it is receiver-dependent:
# the decoded shape is interned per specialization and reused, rather than rebuilt on every
# store. The source is handed over untyped so the analyzer cannot fold the store away, and the
# value is already a canonical tuple carrier so the store's normalization allocates nothing. The
# loop lives on the generic method so the measured work is the store itself, not a per-iteration
# method call. Pair with `tuple_store/` to compare this cached dependent shape against the
# already-predecoded independent shape.
extends RefCounted


class Crate[T]:
	@warning_ignore_start("unused_variable")
	func store_loop(iterations: int, source: Variant) -> void:
		for index in iterations:
			var stored: (int, T) = source


func supply(value) -> Variant:
	return value


func run_benchmark(iterations: int) -> void:
	var crate := Crate[String].new()
	var source: Variant = supply((1, "one"))
	crate.store_loop(iterations, source)
