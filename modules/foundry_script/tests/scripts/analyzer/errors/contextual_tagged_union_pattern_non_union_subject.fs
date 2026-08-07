# A case pattern shorthand needs the subject to name a tagged union; an ordinary value supplies none.
enum Result[T, E]:
	Ok(value: T)
	Err(error: E)


func run(subject: int) -> void:
	match subject:
		.Ok(payload):
			print(payload)
		_:
			pass
