class Box[T]:
	var value: T


class Holder:
	func identity[T](value: T) -> T:
		return value

	func check() -> void:
		var value: Self = identity[Self](self)
		print(value is Holder)

		var box: Box[Self] = Box[Self].new()
		box.value = self
		print(box.value is Holder)


func test() -> void:
	Holder.new().check()
