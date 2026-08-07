extends Object

enum Result[T, E]:
	Ok(value: T)
	Err(error: E)
	Pending

func test(value: Result[int, String]) -> void:
	match value:
		.Ok(inner):
			var other: Result[int, String] = .➡
