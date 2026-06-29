# A required member must be implemented with the trait's type parameter substituted by the use-site
# argument: `uses Holder[int]` requires `add(item: int)`, not `add(item: String)`.
trait Holder[T]:
	abstract func add(item: T) -> void


class Bad uses Holder[int]:
	func add(item: String) -> void:
		pass


func test() -> void:
	pass
