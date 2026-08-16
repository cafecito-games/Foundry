# The return slot is checked at its own boundary, against the receiver that produced the value. The
# caller here stores the result in an untyped local, so nothing downstream would have rejected it:
# the failure has to come from the return itself.
class Crate[T]:
	func produce(value) -> T:
		return value


func test() -> void:
	var crate := Crate[int].new()
	var consumed = crate.produce("not an int")
	print(consumed)
