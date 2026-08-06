# A trait method returning `T` returns the applying class's argument, so binding a `String`-returning
# call to an `int` annotation is rejected at analysis time instead of yielding a mistyped slot.
trait Holder[T]:
	var value: T

	func get_value() -> T:
		return value


class StringBox uses Holder[String]:
	pass


func test() -> void:
	var wrong: int = StringBox.new().get_value()
	print(wrong)
