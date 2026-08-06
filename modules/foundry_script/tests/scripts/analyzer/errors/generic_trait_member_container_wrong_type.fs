# A container built from the trait's type parameter is specialized too, so an `Array[int]` result
# cannot be bound to an `Array[String]` annotation.
trait Bag[T]:
	func all() -> Array[T]:
		var items: Array[T] = []
		return items


class IntBag uses Bag[int]:
	pass


func test() -> void:
	var strings: Array[String] = IntBag.new().all()
	print(strings)
