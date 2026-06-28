extends RefCounted

const Target = preload("res://refactor/callsite_parameter_before_default_target.gd")

func use_target() -> void:
	var target := Target.new()
	target.accept_value("ready")
	target.accept_value("done", 2)
