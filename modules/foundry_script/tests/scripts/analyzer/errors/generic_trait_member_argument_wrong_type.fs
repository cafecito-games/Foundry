# A trait method taking `T` takes the applying class's argument, so passing a `String` to a method
# flattened in through `uses Holder[int]` is rejected at analysis time.
trait Holder[T]:
	var value: T

	func store(item: T) -> void:
		value = item


class IntBox uses Holder[int]:
	pass


func test() -> void:
	IntBox.new().store("nope")
