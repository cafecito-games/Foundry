# An undeclared subject already reported why its type is unknown, so the shorthand arms stay quiet.
enum Option[T]:
	None
	Some(value: T)


func run() -> void:
	match undeclared_subject:
		.Some(payload):
			print(payload)
		.None:
			pass
		_:
			pass
