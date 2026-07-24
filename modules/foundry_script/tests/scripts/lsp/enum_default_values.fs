extends Node

enum Mode:
	IDLE = 0
	RUNNING = IDLE + 1
	PAUSED = RUNNING + 1

var mode: Mode = Mode.RUNNING
var count: int = 3
var alignment: HorizontalAlignment = HORIZONTAL_ALIGNMENT_CENTER

func set_mode(target: Mode = Mode.IDLE, repeats: int = 2) -> void:
	mode = target
	count = repeats
