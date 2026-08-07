# Payload arity is checked against the expected union's specialization, exactly as the explicit
# spelling checks it.
enum Result[T, E]:
	Ok(value: T)
	Err(error: E)


func test():
	var wrong: Result[int, String] = .Ok(1, 2)
	print(wrong)
