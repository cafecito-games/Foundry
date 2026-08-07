# A contextual case shorthand in argument position resolves against the selected callee's parameter
# type, which threads through nested shorthands too.
enum Result[T, E]:
	Ok(value: T)
	Err(error: E)


enum Nested[T]:
	Wrap(inner: Result[T, String])


func describe(value: Result[int, String]) -> String:
	return str(value)


static func described(value: Result[int, String]) -> String:
	return str(value)


func test():
	print(describe(.Ok(1)))
	print(describe(.Err("bad")))
	print(described(.Ok(2)))

	# A shorthand nested inside another case's payload resolves against that payload's field type.
	var nested: Nested[int] = .Wrap(.Ok(4))
	print(nested)

	# The explicit outer form accepts a shorthand payload as well.
	print(Nested[int].Wrap(.Err("inner")))
