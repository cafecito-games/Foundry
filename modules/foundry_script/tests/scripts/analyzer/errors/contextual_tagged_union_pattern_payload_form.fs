# The payload form of a case decides how it is matched, exactly as for the qualified spelling.
enum Result[T, E]:
	Ok(value: T)
	Err(error: E)


func run(subject: Result[int, String]) -> void:
	match subject:
		.Ok:
			pass
		_:
			pass
