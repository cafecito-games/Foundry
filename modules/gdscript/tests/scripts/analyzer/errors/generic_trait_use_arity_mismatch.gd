# Applying a generic trait with the wrong number of type arguments is rejected at the `uses` site.
trait Holder[T]:
	abstract func add(item: T) -> void


class Bad uses Holder[int, String]:
	func add(item: int) -> void:
		pass


func test() -> void:
	pass
