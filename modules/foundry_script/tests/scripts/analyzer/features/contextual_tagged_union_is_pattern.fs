# `is` accepts the same shorthand: the case is named against the tested operand's own union, and the
# payload binds carry that specialization's field types.
enum Result[T, E]:
	Ok(value: T)
	Err(error: E)


enum Option[T]:
	None
	Some(value: T)


func describe(subject: Result[int, String]) -> String:
	if subject is .Ok(value):
		return "ok " + str(value + 1)
	if subject is .Err(error):
		return "err " + error.to_upper()
	return "unreachable"


func is_empty(subject: Option[int]) -> bool:
	return subject is .None


func not_empty(subject: Option[int]) -> bool:
	return subject is not .None


func both(subject: Result[int, String]) -> String:
	if subject is .Ok(value) and value > 1:
		return "big " + str(value)
	return "small"


func test():
	print(describe(.Ok(1)))
	print(describe(.Err("bad")))
	print(is_empty(.None))
	print(is_empty(.Some(1)))
	print(not_empty(.None))
	print(both(.Ok(2)))
	print(both(.Ok(0)))
