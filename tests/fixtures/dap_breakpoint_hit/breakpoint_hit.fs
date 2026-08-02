extends Node

var member_value: int = 35

func _ready() -> void:
	var local_value: int = 7
	member_value += local_value - local_value
	get_tree().quit(0)
