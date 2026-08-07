# An undeclared operand already reported why its type is unknown, so the shorthand stays quiet.
enum Result[T, E]:
	Ok(value: T)
	Err(error: E)


func run() -> void:
	if undeclared_operand is .Ok(value):
		print(value)
