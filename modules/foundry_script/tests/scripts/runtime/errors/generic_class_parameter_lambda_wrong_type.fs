# A lambda that stores into a class-parameter slot is checked against the receiver like any other
# body. Its own body never names `self`, so the check only works because declaring such a slot makes
# the lambda capture the instance; without that capture the frame would have no receiver and the
# store would silently accept anything.
class Crate[T]:
	func keep_in_lambda(value):
		var keeper := func(v):
			var kept: T = v
			return kept
		return keeper.call(value)


func test() -> void:
	var crate := Crate[int].new()
	print(crate.keep_in_lambda("not an int"))
