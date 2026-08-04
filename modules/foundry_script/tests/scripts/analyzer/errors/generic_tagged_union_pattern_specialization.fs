# Two specializations of one declaration are unrelated types, so a case pattern taken from a
# different specialization can never match the subject.
enum Result[T, E]:
	Ok(value: T)
	Err(error: E)

func inspect(value: Result[int, String]) -> void:
	match value:
		Result[float, String].Ok(number):
			print(number)
		_:
			pass
