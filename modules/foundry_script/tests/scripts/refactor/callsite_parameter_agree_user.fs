extends Node

const Target = preload("res://refactor/callsite_parameter_agree_target.fs")

func run() -> void:
	var target := Target.new()
	target.accept_score(1)
	target.accept_score(2 + 3)
