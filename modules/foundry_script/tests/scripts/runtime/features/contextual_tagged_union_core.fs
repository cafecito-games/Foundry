# The contextual shorthand and the explicit spelling build the same value: the same read-only
# `[tag, payload...]` Array, equal by content and usable as a Dictionary key interchangeably.
enum Result[T, E]:
	Ok(value: T)
	Err(error: E)


enum Option[T]:
	None
	Some(value: T)


func consume(value: Result[int, String]) -> Result[int, String]:
	return value


func defaulted(value: Result[int, String] = .Ok(5)) -> Result[int, String]:
	return value


func test():
	var shorthand: Result[int, String] = .Ok(1)
	var explicit: Result[int, String] = Result[int, String].Ok(1)
	print(shorthand)
	print(explicit)
	print(shorthand == explicit)

	var empty_shorthand: Option[int] = .None
	var empty_explicit: Option[int] = Option[int].None
	print(empty_shorthand == empty_explicit)

	var assigned: Result[int, String] = Result[int, String].Ok(0)
	assigned = .Err("bad")
	print(assigned)
	print(assigned == Result[int, String].Err("bad"))

	print(consume(.Ok(2)))
	print(defaulted())
	print(defaulted() == Result[int, String].Ok(5))

	var keyed := {}
	keyed[shorthand] = "from shorthand"
	print(keyed[explicit])
