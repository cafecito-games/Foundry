# The shorthand also heads a match case pattern, where the union comes from the subject.
# Resolving the case against the subject's union is not implemented yet, so the analyzer error
# below is the expected interim state.
enum Result[T, E]:
	Ok(value: int)
	Err(error: String)


func run(subject: int) -> void:
	match subject:
		.Ok(payload):
			print(payload)
		.Err(reason):
			print(reason)
		_:
			pass
