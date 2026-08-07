# The case name is checked against the subject union's declared cases in both pattern forms.
enum Result[T, E]:
	Ok(value: T)
	Err(error: E)


func run(subject: Result[int, String]) -> void:
	match subject:
		.Nope(payload):
			print(payload)
		.Missing:
			pass
		_:
			pass
