# Feature side: every iteration stores into a local declared `Box[int]`. The declaration is
# specialized, so its type operand travels as a container-type descriptor rather than as a bare script
# constant, and the store's cost is whatever obtaining that shape costs plus the check itself. The
# source is handed over untyped so the analyzer cannot fold the store away.
extends RefCounted


class Box[T]:
	var value: T


func supply(value) -> Variant:
	return value


@warning_ignore_start("unused_variable")
func run_benchmark(iterations: int) -> void:
	var source: Variant = supply(Box[int].new())
	for index in iterations:
		var stored: Box[int] = source
