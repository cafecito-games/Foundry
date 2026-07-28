extends Node

tuple Vec2(x: int, y: int)

var counter := 0

func bump() -> void:
	var point := Vec2(1, 2)
	counter += point.x + point.0
