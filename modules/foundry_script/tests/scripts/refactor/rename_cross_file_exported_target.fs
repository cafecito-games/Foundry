extends Node

@export var shared_exported_count := 0

func bump() -> void:
	shared_exported_count += 1

func read() -> int:
	return shared_exported_count
