# Applying concrete arguments to a generic tagged union is rejected until argument binding lands.
enum Outcome[T, E]:
	Ok(value: T)
	Err(error: E)

func take(outcome: Outcome[int, String]) -> void:
	print(outcome)
