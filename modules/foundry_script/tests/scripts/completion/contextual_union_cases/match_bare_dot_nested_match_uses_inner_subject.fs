extends Object

enum Result[T, E]:
	Ok(value: T)
	Err(error: E)
	Pending

enum Step:
	Begin(index: int)
	Middle
	End

func test(value: Result[int, String], step: Step) -> void:
	match value:
		.Ok(inner):
			match step:
				.Begin(index):
					pass
				.➡
		.Err(problem):
			pass
