# A contextual case shorthand written as a named tuple's field resolves against the declared field
# type, as it already did as an array element and as an unnamed tuple element.
enum Result[T, E]:
	Ok(value: T)
	Err(error: E)


enum Option[T]:
	Some(value: T)
	None


enum Nested[T]:
	Wrap(inner: Result[T, String])


tuple Slot(outcome: Result[int, String], index: int)
tuple Maybe(value: Option[int], label: String)
tuple Deep(wrapped: Nested[int], tag: String)


func test():
	var slot := Slot(.Ok(1), 2)
	print(slot)
	print(slot.index)
	match slot.outcome:
		.Ok(value):
			print("ok ", value)
		.Err(error):
			print("err ", error)

	print(Slot(.Err("bad"), 3))

	# A payload-less case folds to its singleton in field position too.
	print(Maybe(.None, "empty"))
	print(Maybe(.Some(5), "five"))

	# A shorthand nested inside another case's payload resolves at depth.
	print(Deep(.Wrap(.Ok(4)), "deep"))
