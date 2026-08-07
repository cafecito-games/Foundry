# A payload-carrying case must be constructed and a payload-less case must not be, in the shorthand
# just as in the explicit spelling.
enum Option[T]:
	None
	Some(value: T)


func test():
	var uncalled: Option[int] = .Some
	var called: Option[int] = .None()
	print(uncalled, called)
