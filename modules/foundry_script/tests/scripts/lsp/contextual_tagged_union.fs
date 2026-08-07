class_name LspContextualTaggedUnion

enum Outcome[T, E]:
	Ok(value: T)
	Err(error: E)
	Pending

func produce() -> Outcome[int, String]:
	var result: Outcome[int, String] = .Ok(1)
	var waiting: Outcome[int, String] = .Pending
	if waiting is .Pending:
		return waiting
	return result

func classify(subject: Outcome[int, String]) -> int:
	match subject:
		.Ok(number):
			return number
		.Err(_reason):
			return -1
		.Pending:
			return 0
