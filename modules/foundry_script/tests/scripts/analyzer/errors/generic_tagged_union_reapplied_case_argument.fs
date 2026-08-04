# Applying arguments to an already-specialized handle re-derives the payload schema from the
# declaration, so the last application wins for the arguments and for the payload types alike. A
# handle whose arguments and payload constraints disagreed would accept a value of the wrong type.
enum Result[T, E]:
	Ok(value: T)
	Err(error: E)

func build() -> void:
	var reapplied = Result[int, String][float, bool]
	var wrong = reapplied.Ok("not a float")
	print(wrong)
