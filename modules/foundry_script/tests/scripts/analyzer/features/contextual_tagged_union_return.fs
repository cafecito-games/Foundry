# A contextual case shorthand in return position resolves against the enclosing function's declared
# return type, including a lambda's.
enum Result[T, E]:
	Ok(value: T)
	Err(error: E)


func make_ok() -> Result[int, String]:
	return .Ok(3)


func make_err() -> Result[int, String]:
	return .Err("bad")


func test():
	print(make_ok())
	print(make_err())

	var produce := func() -> Result[int, String]:
		return .Ok(9)
	print(produce.call())
