extends Node

var counter: int = 0

# A retroactive conformance sits between ordinary members.
extend Node2D uses Greeter,Farewell:
	func greet()->String:
		return "hi"
	func farewell() -> String:
		return "bye"

func run() -> void:
	print(counter)

extend int uses Marker:
	pass  # nothing to witness

trait Greeter:
	func greet() -> String

trait Farewell:
	func farewell() -> String

trait Marker:
	pass
