extends Node

const Target = preload("res://refactor/rename_cross_file_target.gd")

func use_target() -> int:
	var target := Target.new()
	target.shared_count += 1
	return target.shared_count
