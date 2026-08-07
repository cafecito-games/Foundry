# A `match` case pattern may name a case of the subject's union without naming the union, and the
# payload binds take the specialized field types. The shorthand and the qualified spelling are
# interchangeable within one `match`, including under a guard.
enum Result[T, E]:
	Ok(value: T)
	Err(error: E)


enum Option[T]:
	None
	Some(value: T)


func describe(subject: Result[int, String]) -> String:
	match subject:
		.Ok(value) when value > 10:
			return "big " + str(value + 1)
		.Ok(value):
			return "ok " + str(value + 1)
		Result[int, String].Err(error):
			return "err " + error
		_:
			return "unreachable"


func describe_option(subject: Option[int]) -> String:
	match subject:
		.None:
			return "none"
		.Some(value):
			return "some " + str(value + 1)
		_:
			return "unreachable"


func test():
	print(describe(.Ok(1)))
	print(describe(.Ok(11)))
	print(describe(.Err("bad")))
	print(describe_option(.None))
	print(describe_option(.Some(2)))
