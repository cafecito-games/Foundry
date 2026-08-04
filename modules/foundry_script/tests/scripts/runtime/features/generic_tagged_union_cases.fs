# A specialized case constructs and matches at runtime exactly as a non-generic one does: the value
# is the read-only `[tag, payload...]` Array, the payload binds carry the concrete values, and two
# specializations of one declaration produce independent values.
enum Result[T, E]:
	Ok(value: T)
	Err(error: E)

enum Bundle[T]:
	Items(values: Array[T])
	Empty

func describe(value: Result[int, String]) -> String:
	match value:
		Result[int, String].Ok(var number):
			return "ok %d" % (number * 2)
		Result[int, String].Err(var message):
			return "err %d" % message.length()
	return "unreachable"

func describe_flipped(value: Result[String, int]) -> String:
	if value is Result[String, int].Ok(text):
		return "ok " + text.to_upper()
	if value is Result[String, int].Err(code):
		return "err %d" % (code + 1)
	return "unreachable"

func total(bundle: Bundle[int]) -> int:
	match bundle:
		Bundle[int].Items(var values):
			var sum := 0
			for value in values:
				sum += value
			return sum
		Bundle[int].Empty:
			return 0
	return -1

func test():
	print(describe(Result[int, String].Ok(21)))
	print(describe(Result[int, String].Err("four")))
	print(describe_flipped(Result[String, int].Ok("hi")))
	print(describe_flipped(Result[String, int].Err(41)))

	print(total(Bundle[int].Items([1, 2, 3])))
	print(total(Bundle[int].Empty))

	# Values still compare by content, just like a non-generic union's values do.
	print(Result[int, String].Ok(1) == Result[int, String].Ok(1))
	print(Result[int, String].Ok(1) == Result[int, String].Err("1"))
