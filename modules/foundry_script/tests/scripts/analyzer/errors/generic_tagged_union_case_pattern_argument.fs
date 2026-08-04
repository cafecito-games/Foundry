# A case-pattern head resolves its type arguments the same way a value-position application does, so
# an argument it cannot read reports once, on the argument itself, instead of being followed by a
# derived complaint about the specialization the Variant fallback would name.
enum Result[T, E]:
	Ok(value: T)
	Err(error: E)

func inspect(value: Result[(int, String), int]) -> void:
	match value:
		Result[(int, String), int].Ok(var pair):
			print(pair)
		_:
			pass
