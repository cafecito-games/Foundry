# Feature side: every iteration returns through a declared `Box[int]` return type. The declaration is
# specialized, so its type operand travels as a container-type descriptor and the return decodes it to
# answer the structural question. The returned value is held untyped so the analyzer cannot prove the
# return statically safe and drop the boundary.
extends RefCounted


class Box[T]:
	var value: T


var source: Variant


func supply(value) -> Variant:
	return value


func produce() -> Box[int]:
	return source


@warning_ignore_start("unused_variable")
func run_benchmark(iterations: int) -> void:
	source = supply(Box[int].new())
	for index in iterations:
		var produced = produce()
