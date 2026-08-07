# A payload-less contextual case shorthand is a value of the expected union in every core context,
# and folds to the same read-only `[tag]` singleton the explicit spelling folds to.
enum Option[T]:
	None
	Some(value: T)


func take(value: Option[int]) -> String:
	return str(value)


func produce() -> Option[int]:
	return .None


func test():
	var empty: Option[int] = .None
	print(empty)
	print(empty == Option[int].None)

	var reassigned: Option[int] = Option[int].Some(1)
	reassigned = .None
	print(reassigned)

	print(take(.None))
	print(produce())
