# Both branches of a conditional expression are qualified by the type the conditional itself is
# expected to have, in every position that supplies one.
enum Result[T, E]:
	Ok(value: T)
	Err(error: E)


func choose(condition: bool) -> Result[int, String]:
	return .Ok(1) if condition else .Err("no")


func consume(value: Result[int, String]) -> String:
	return str(value)


func test(condition: bool = true):
	var chosen: Result[int, String] = .Ok(1) if condition else .Err("no")
	print(chosen)
	print(choose(true))
	print(choose(false))

	# Only the shorthand branch needs qualifying; an explicit branch keeps its own resolution.
	var mixed: Result[int, String] = .Ok(2) if condition else Result[int, String].Err("e")
	print(mixed)

	# Argument position supplies the expected type to the conditional the same way.
	print(consume(.Ok(3) if condition else .Err("no")))

	# A conditional in element position is qualified by the container's element type.
	var elements: Array[Result[int, String]] = [.Ok(4) if condition else .Err("no")]
	print(elements)

	var assigned: Result[int, String] = Result[int, String].Ok(0)
	assigned = .Ok(5) if condition else .Err("no")
	print(assigned)
