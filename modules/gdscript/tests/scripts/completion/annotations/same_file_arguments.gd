extends Node

annotation my_timeout(seconds: float, label: String = "default") targets METHOD

@my_timeout(➡)
func foo() -> void:
	pass
