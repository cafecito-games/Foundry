extends Node

var counter := 0

func bump() -> void:
	counter += 1

func read() -> int:
	return counter
