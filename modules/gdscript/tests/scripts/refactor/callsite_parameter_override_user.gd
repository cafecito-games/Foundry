extends RefCounted

const Target = preload("res://refactor/callsite_parameter_override_target.gd")

func use_target() -> void:
	var target := Target.new()
	target.accept_override(1)
