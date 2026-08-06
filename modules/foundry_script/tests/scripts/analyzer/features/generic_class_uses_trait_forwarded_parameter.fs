# A generic class may forward its own type parameter into a generic trait. Each specialization of the
# class binds the trait's parameter through that forwarding hop, so `Pair[int]` and `Pair[String]`
# see the flattened members at their own argument types.
trait Holder[T]:
	var value: T

	func store(item: T) -> void:
		value = item

	func get_value() -> T:
		return value


class Pair[T] uses Holder[T]:
	var second: T


func test() -> void:
	var numbers := Pair[int].new()
	numbers.store(3)
	var number: int = numbers.get_value()
	numbers.value = 4

	var words := Pair[String].new()
	words.store("hi")
	var word: String = words.get_value()

	print(number)
	print(numbers.value)
	print(word)
	print("generic class uses trait forwarded parameter ok")
