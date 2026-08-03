# A tagged union's type arguments are invariant, so two applications differing in any one argument
# are unrelated types even when the argument types themselves convert.
enum Result[T, E]:
	Ok(value: T)
	Err(error: E)

func take(value: Result[int, String]) -> void:
	var widened: Result[float, String] = value
	print(widened)
