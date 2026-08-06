# A variable flattened in through `uses Holder[int]` is typed by the argument the class supplied, so
# writing a `String` into the `T`-typed variable is rejected at analysis time.
trait Holder[T]:
	var value: T


class IntBox uses Holder[int]:
	pass


func test() -> void:
	var box := IntBox.new()
	box.value = "not an int"
