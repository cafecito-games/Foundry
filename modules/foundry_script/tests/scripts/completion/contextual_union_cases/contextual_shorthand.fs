extends Object

enum Result[T, E]:
	Ok(value: T)
	Err(error: E)
	Pending

func test() -> void:
	var value: Result[int, String] = .Pending➡
	pass
