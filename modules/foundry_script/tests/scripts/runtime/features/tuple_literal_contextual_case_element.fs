# A contextual case shorthand written as a tuple element resolves against the declared element type,
# as it already did as an array element.
enum Result[T, E]:
	Ok(value: T)
	Err(error: E)


func test():
	var pair: (Result[int, String], int) = (.Ok(1), 2)
	print(pair)
	match pair[0]:
		.Ok(value):
			print("ok ", value)
		.Err(error):
			print("err ", error)
