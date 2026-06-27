# Feature side: every call goes through a dynamic proxy. `create_proxy_dynamic`
# builds an object that routes each trait method to the handler callable, so the
# fork's proxy resolution + Variant marshaling path runs once per iteration. The
# handler computes the same value the baseline computes directly.
extends RefCounted

trait Computer:
	abstract func compute(value: int) -> int

func run_benchmark(iterations: int) -> void:
	var computer := create_proxy_dynamic(Computer, func(_method_name: StringName, args: Array) -> Variant:
		var value: int = args[0]
		return value * 2 - value) as Computer
	var accumulator: int = 0
	for index in iterations:
		accumulator += computer.compute(index) - accumulator
