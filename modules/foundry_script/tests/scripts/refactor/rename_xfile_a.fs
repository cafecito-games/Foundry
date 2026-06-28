extends Node

var shared_value := 0

func bump() -> void:
	shared_value += 1

func read() -> int:
	return shared_value
