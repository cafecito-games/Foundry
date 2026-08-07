extends Object

enum Result[T, E]:
	Ok(value: T)
	Err(error: E)
	Pending

func test(value: Result[int, String]) -> void:
	if value is .➡
		pass
