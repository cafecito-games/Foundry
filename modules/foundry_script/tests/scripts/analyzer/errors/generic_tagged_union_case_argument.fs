# A case constructor checks its arguments against the payload types of the applied specialization,
# not against the declaration's type parameters.
enum Result[T, E]:
	Ok(value: T)
	Err(error: E)

func build() -> void:
	var wrong := Result[int, String].Ok("one")
	print(wrong)
