# A lambda storing into a class-dependent specialized handle slot is checked like any other body. Its
# own body never names `self`, so the check only works because declaring such a slot makes the lambda
# capture the instance.
class Holder[T: Type[Node]]:
	var value: T


class Wrapper[U: Node]:
	func keep_in_lambda(value):
		var keeper := func(v):
			var kept: Holder[Type[U]] = v
			return kept
		return keeper.call(value)


func test() -> void:
	var wrapper := Wrapper[Button].new()
	print(wrapper.keep_in_lambda(Holder[Type[Label]].new()))
