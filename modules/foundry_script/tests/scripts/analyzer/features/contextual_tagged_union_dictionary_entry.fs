# A dictionary literal qualifies a contextual case shorthand from the key or the value element type,
# whichever position the shorthand sits in.
enum Result[T, E]:
	Ok(value: T)
	Err(error: E)


func test():
	var by_id: Dictionary[int, Result[int, String]] = {1: .Ok(1), 2: .Err("x")}
	print(by_id)
	print(by_id[1] == Result[int, String].Ok(1))

	var labels: Dictionary[Result[int, String], String] = {.Ok(1): "ok"}
	print(labels)
	print(labels[Result[int, String].Ok(1)])
