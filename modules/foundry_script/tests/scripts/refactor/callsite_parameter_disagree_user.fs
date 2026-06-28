extends Node

const Target = preload("res://refactor/callsite_parameter_disagree_target.fs")

func run() -> void:
	var target := Target.new()
	target.accept_value(1)
	target.accept_value("name")
