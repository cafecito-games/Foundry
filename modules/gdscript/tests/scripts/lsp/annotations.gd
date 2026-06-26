extends Node

annotation my_marker targets METHOD
annotation my_timeout(seconds: float) targets METHOD

@my_marker
@my_timeout(10.0)
func foo() -> void:
	pass
