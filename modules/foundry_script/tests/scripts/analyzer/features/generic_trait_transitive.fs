# A generic trait used by another trait composes under substitution. `Reader[T]`/`Writer[T]` forward
# their own type parameter into the generic supertrait `Storage[T]`, and a class that applies them
# with `int` must satisfy the forwarded `Storage` requirement with `T := int`. A diamond — two
# intermediate traits forwarding to the same generic supertrait — composes consistently.
trait Storage[T]:
	abstract func add(item: T) -> void


trait Reader[T] uses Storage[T]:
	abstract func read() -> T


trait Writer[T] uses Storage[T]:
	abstract func write(item: T) -> void


class IntStore uses Reader[int], Writer[int]:
	var slot: int = 0

	func add(item: int) -> void:
		slot = item

	func read() -> int:
		return slot

	func write(item: int) -> void:
		add(item)


func test() -> void:
	var store := IntStore.new()
	store.write(9)
	print(store.read())
	print(store is Storage)
	print(store is Reader)
	print("generic trait transitive ok")
