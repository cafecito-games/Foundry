# The case name of an `is` shorthand is checked against the operand union's declared cases.
enum Result[T, E]:
	Ok(value: T)
	Err(error: E)


func run(subject: Result[int, String]) -> void:
	if subject is .Nope(value):
		print(value)
