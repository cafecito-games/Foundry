extends Node

# Main scene of the debug adapter exit-status fixture project. It ends the process
# with a distinctive nonzero result, so a plain scene launch can be checked against
# the same lifecycle a structured test launch uses.
const EXIT_CODE := 3

func _ready() -> void:
	get_tree().quit(EXIT_CODE)
