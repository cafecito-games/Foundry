# The leading-`.` contextual case shorthand parses in every expression position, with and
# without a payload. None of the declarations below supplies a union, so the two the resolver
# reaches are reported as missing their expected tagged-union type; container elements and
# conditional-expression branches do not propagate an expected type into the shorthand yet.
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
