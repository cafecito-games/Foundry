extends Node

enum Mode { IDLE, RUNNING, PAUSED }

var mode: Mode = Mode.RUNNING
var count: int = 3
var alignment: HorizontalAlignment = HORIZONTAL_ALIGNMENT_CENTER

func set_mode(target: Mode = Mode.IDLE, repeats: int = 2) -> void:
	mode = target
	count = repeats
