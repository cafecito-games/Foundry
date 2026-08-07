# A nullable union subject adds `null` to the domain, which no case pattern covers, so a match over
# every case with the shorthand still leaves `null` unhandled.
enum Result[T, E]:
	Ok(value: T)
	Err(error: E)

func describe(subject: Result[int, String]?) -> void:
	match subject:
		.Ok(value):
			print(value)
		.Err(error):
			print(error)

func test():
	describe(Result[int, String].Ok(1))
	describe(null)
