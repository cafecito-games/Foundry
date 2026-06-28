extends Node

func run() -> void:
	var target := preload("res://refactor/callsite_parameter_dynamic_target.gd").new()
	target.accept_dynamic(1)
	target.call("accept_dynamic", 2)
