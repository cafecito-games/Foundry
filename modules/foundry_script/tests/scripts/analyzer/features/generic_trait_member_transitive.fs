# A concrete member declared on a generic supertrait is specialized through the forwarding hop:
# `uses Reader[int]` binds `Reader`'s `T` to `int`, which `Reader` forwards into `Storage[T]`.
trait Storage[T]:
	var slot: T

	func put(item: T) -> void:
		slot = item

	func fetch() -> T:
		return slot


trait Reader[T] uses Storage[T]:
	func fetch_or(fallback: T) -> T:
		if slot == null:
			return fallback
		return fetch()


class IntStore uses Reader[int]:
	pass


func test() -> void:
	var store := IntStore.new()
	store.put(4)
	var value: int = store.fetch()
	var fallback_value: int = store.fetch_or(-1)
	store.slot = 5
	print(value)
	print(fallback_value)
	print(store.slot)
	print("generic trait member transitive ok")
