extends RefCounted

const Target = preload("res://refactor/callsite_parameter_second_arg_target.fs")

func use_target() -> void:
	var target := Target.new()
	target.accept_second("ready", 1)
	target.accept_second("done", 2 + 3)
