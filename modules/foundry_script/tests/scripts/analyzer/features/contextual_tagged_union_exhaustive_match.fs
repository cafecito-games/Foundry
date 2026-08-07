# A shorthand case pattern carries the subject's own union, so exhaustiveness counts it exactly as it
# counts the qualified spelling: an all-shorthand match over every case needs no wildcard branch and
# leaves no fallthrough, so a value-returning function needs no trailing `return`.
enum Result[T, E]:
	Ok(value: T)
	Err(error: E)


enum Option[T]:
	None
	Some(value: T)


func unwrap_or_zero(subject: Result[int, String]) -> int:
	match subject:
		.Ok(value):
			return value
		.Err(_):
			return 0


func mixed_spellings(subject: Result[int, String]) -> int:
	match subject:
		.Ok(value):
			return value
		Result[int, String].Err(_):
			return -1


func payloadless(subject: Option[int]) -> int:
	match subject:
		.None:
			return 0
		.Some(value):
			return value


func test():
	print(unwrap_or_zero(.Ok(3)))
	print(unwrap_or_zero(.Err("bad")))
	print(mixed_spellings(.Ok(4)))
	print(mixed_spellings(.Err("bad")))
	print(payloadless(.None))
	print(payloadless(.Some(5)))
