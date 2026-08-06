# The forwarding hop is specialized too: `uses Reader[int]` binds `Reader`'s `T` to `int`, which
# `Reader` forwards into `Storage[T]`, so `Storage`'s `put()` takes an `int` on `IntStore`.
trait Storage[T]:
	var slot: T

	func put(item: T) -> void:
		slot = item


trait Reader[T] uses Storage[T]:
	func fetch() -> T:
		return slot


class IntStore uses Reader[int]:
	pass


func test() -> void:
	IntStore.new().put("nope")
