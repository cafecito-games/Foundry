# The `is` shorthand needs the tested operand to name a complete tagged-union specialization.
enum Result[T, E]:
	Ok(value: T)
	Err(error: E)


func run(subject: int) -> void:
	if subject is .Ok(value):
		print(value)
