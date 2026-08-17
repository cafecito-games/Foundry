# `Self` resolves to the receiver's leaf script, so an instance of the base does not satisfy a
# `Derived` receiver's `Self` element.
class Node2 extends RefCounted:
	func take(pair: (int, Self)) -> void:
		print("took ", pair.0)


class Derived extends Node2:
	pass


func supply(value: Variant) -> Variant:
	return value


func test() -> void:
	Derived.new().take(supply((1, Derived.new())))
	Derived.new().take(supply((2, Node2.new())))
	print("unreachable")
