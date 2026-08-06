# The leading-`.` contextual case shorthand parses in every expression position, with and
# without a payload. Resolving it against the expected type is not implemented yet, so the
# shorthand still reaches code generation unqualified and is refused there.
enum Result[T, E]:
	Ok(value: int)
	Err(error: String)


enum Option[T]:
	None
	Some(value: int)


func run(condition: bool) -> void:
	var single = .Ok(1)
	var empty = .None
	var elements = [.Ok(1), .Err("x")]
	var chosen = .Ok(1) if condition else .Err("no")
	print(single, empty, elements, chosen)
