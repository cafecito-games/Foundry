# A generic class forwarding its own type parameter into a trait (`class Pair[T] uses Holder[T]`)
# specializes the flattened members against the receiver's argument, so a `Pair[int]` receiver sees
# `get_value()` as returning `int`.
trait Holder[T]:
	var value: T

	func get_value() -> T:
		return value


class Pair[T] uses Holder[T]:
	var second: T


func test() -> void:
	var wrong: String = Pair[int].new().get_value()
	print(wrong)
