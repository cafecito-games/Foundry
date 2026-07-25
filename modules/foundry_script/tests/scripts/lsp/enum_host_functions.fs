extends Node

enum Status:
	READY = 1
	DONE = 2

	## Formats this status.
	func label(prefix: String = "") -> String:
		return prefix

	## Refreshes this status asynchronously.
	async func refresh() -> String:
		return ""

	## Parses a status.
	static func parse(text: String) -> Status:
		return READY

func use(status: Status) -> void:
	var parsed = Status.parse("ready")
	var text = status.label(">")
	var pending = status.refresh()
