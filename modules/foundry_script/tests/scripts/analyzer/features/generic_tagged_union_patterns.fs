# A case pattern and an `is` bind read the payload types of the applied specialization, so a bind
# lands on the concrete type rather than on the declaration's type parameter. Operations that only a
# concrete type admits — integer arithmetic, `String.length()` — are what make the substitution
# observable.
enum Result[T, E]:
	Ok(value: T)
	Err(error: E)

enum Pair[A, B]:
	Both(first: A, second: B)

func inspect(value: Result[int, String]) -> int:
	if value is Result[int, String].Ok(number):
		return number + 1
	match value:
		Result[int, String].Err(var message):
			return message.length()
		_:
			return 0

# The mirrored application of the same declaration binds the parameters the other way round, in the
# same analyzed source.
func inspect_flipped(value: Result[String, int]) -> int:
	match value:
		Result[String, int].Ok(var text):
			return text.length()
		Result[String, int].Err(var code):
			return code + 1
	return 0

func combine(value: Pair[String, int]) -> String:
	if value is Pair[String, int].Both(text, count):
		return "%s:%d" % [text, count]
	return ""

# Narrowing a subject to one of its cases keeps the specialization: the narrowed value still assigns
# to the applied union type, which is invariant in its arguments.
func narrowed(value: Result[int, String]) -> int:
	match value:
		Result[int, String].Ok(_):
			var same: Result[int, String] = value
			print(same)
			return 1
		_:
			return 0

func test():
	print(inspect(Result[int, String].Ok(41)))
	print(inspect(Result[int, String].Err("four")))
	print(inspect_flipped(Result[String, int].Ok("hello")))
	print(inspect_flipped(Result[String, int].Err(9)))
	print(combine(Pair[String, int].Both("x", 3)))
	print(narrowed(Result[int, String].Ok(1)))
	print(narrowed(Result[int, String].Err("no")))
