# A shorthand resolved in a nested position builds exactly the value the explicit spelling builds,
# through container elements, casts, and both branches of a conditional expression.
enum Result[T, E]:
	Ok(value: T)
	Err(error: E)


enum Option[T]:
	None
	Some(value: T)


func pick(condition: bool) -> Result[int, String]:
	return .Ok(1) if condition else .Err("no")


func test():
	var pairs: Array[Result[int, String]] = [.Ok(1), .Err("x")]
	var explicit_pairs: Array[Result[int, String]] = [Result[int, String].Ok(1), Result[int, String].Err("x")]
	print(pairs)
	print(pairs == explicit_pairs)

	var by_id: Dictionary[int, Result[int, String]] = {1: .Ok(1)}
	print(by_id[1] == Result[int, String].Ok(1))

	var grid: Array[Array[Result[int, String]]] = [[.Ok(1)], [.Err("x")]]
	print(grid)

	var labeled = .Ok(2) as Result[int, String]
	print(labeled == Result[int, String].Ok(2))

	var empties: Array[Option[int]] = [.None]
	print(empties[0] == Option[int].None)

	print(pick(true) == Result[int, String].Ok(1))
	print(pick(false) == Result[int, String].Err("no"))

	var nested: Array[Result[Result[int, String], String]] = [.Ok(.Ok(1))]
	print(nested[0] == Result[Result[int, String], String].Ok(Result[int, String].Ok(1)))
