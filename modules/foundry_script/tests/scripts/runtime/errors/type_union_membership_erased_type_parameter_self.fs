# The same answer when every alternative names `Self`: the alternatives are resolved against the
# running receiver and the value is still none of them.
class Receiver:
	func take(v: (int, Self) | (String, Self)) -> void:
		print("took ", v)

	func launder[T](value: T) -> void:
		take(value)

	func drive() -> void:
		launder(5)


func test() -> void:
	Receiver.new().drive()
