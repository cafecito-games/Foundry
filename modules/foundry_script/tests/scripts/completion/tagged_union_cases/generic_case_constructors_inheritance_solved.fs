extends Object

enum Result[T, E]:
	Ok(value: T)
	Err(error: E)

func test() -> void:
	Result[int, String].➡
	pass
