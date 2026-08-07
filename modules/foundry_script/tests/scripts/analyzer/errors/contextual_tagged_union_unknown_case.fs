# The case name is checked against the expected union's declared cases.
enum Result[T, E]:
	Ok(value: T)
	Err(error: E)


func test():
	var wrong: Result[int, String] = .Nope(1)
	print(wrong)
