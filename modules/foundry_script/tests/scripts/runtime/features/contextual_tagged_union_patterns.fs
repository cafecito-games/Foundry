# End to end: the shorthand and the qualified spelling select the same branch and bind the same
# specialized payload values, in a `match` pattern and in an `is` test alike.
enum Result[T, E]:
	Ok(value: T)
	Err(error: E)


enum Option[T]:
	None
	Some(value: T)


func shorthand_match(subject: Result[int, String]) -> String:
	match subject:
		.Ok(value):
			return "ok " + str(value * 2)
		.Err(error):
			return "err " + error.to_upper()


func qualified_match(subject: Result[int, String]) -> String:
	match subject:
		Result[int, String].Ok(value):
			return "ok " + str(value * 2)
		Result[int, String].Err(error):
			return "err " + error.to_upper()


func shorthand_test(subject: Result[int, String]) -> String:
	if subject is .Ok(value):
		return "ok " + str(value * 2)
	if subject is .Err(error):
		return "err " + error.to_upper()
	return "unreachable"


func option_label(subject: Option[String]) -> String:
	match subject:
		.None:
			return "empty"
		.Some(value):
			return "held " + value


func test():
	var ok: Result[int, String] = .Ok(3)
	var err: Result[int, String] = .Err("bad")

	print(shorthand_match(ok))
	print(shorthand_match(err))
	print(shorthand_match(ok) == qualified_match(ok))
	print(shorthand_match(err) == qualified_match(err))

	print(shorthand_test(ok))
	print(shorthand_test(err))

	print(option_label(.None))
	print(option_label(.Some("x")))

	# A shorthand bind carries the specialization's field type, not Variant.
	if ok is .Ok(value):
		var doubled: int = value * 2
		print(doubled)
