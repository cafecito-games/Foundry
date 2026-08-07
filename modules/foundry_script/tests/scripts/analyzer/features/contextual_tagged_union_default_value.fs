# A contextual case shorthand is a valid parameter default: the payload form constant-folds to the
# read-only `[tag, payload...]` Array the case erases to, so it survives as a baked default value.
enum Result[T, E]:
	Ok(value: T)
	Err(error: E)


enum Option[T]:
	None
	Some(value: T)


func with_payload(value: Result[int, String] = .Ok(1)) -> String:
	return str(value)


func without_payload(value: Option[int] = .None) -> String:
	return str(value)


func with_explicit(value: Result[int, String] = Result[int, String].Ok(1)) -> String:
	return str(value)


func test():
	print(with_payload())
	print(with_payload(Result[int, String].Err("given")))
	print(without_payload())
	print(with_explicit())
	print(with_payload() == with_explicit())
