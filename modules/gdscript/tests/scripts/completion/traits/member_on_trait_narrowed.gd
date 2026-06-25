trait LocalLoggable:
	func log_event() -> void:
		pass

func use_it(value) -> void:
	if value is LocalLoggable:
		value.➡
