# A use-site type argument that does not satisfy the generic trait's type-parameter bound is rejected.
trait Holder[T: RefCounted]:
	abstract func add(item: T) -> void


class Bad uses Holder[int]:
	func add(item: int) -> void:
		pass


func test() -> void:
	pass
