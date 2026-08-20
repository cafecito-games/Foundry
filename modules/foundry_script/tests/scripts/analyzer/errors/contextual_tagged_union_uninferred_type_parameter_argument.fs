# A type parameter no argument could constrain falls back to Variant so the rest of the call stays
# checkable, but nothing in that position expects a Variant: applying the type arguments explicitly
# is what would give the shorthand a union, so the message keeps the annotation suggestion instead of
# naming the recovery type.
enum Result[T, E]:
	Ok(value: T)
	Err(error: E)


func consume[T](value: T):
	print(value)


func test():
	consume(.Ok(1))
	consume[Result[int, String]](.Ok(1))
