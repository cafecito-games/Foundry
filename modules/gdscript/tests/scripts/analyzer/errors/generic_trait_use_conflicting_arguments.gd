# A generic trait reached through two different used traits must be bound to the same type arguments
# on every path; conflicting specializations are a contradiction and are rejected.
trait Storage[T]:
	@abstract func add(item: T) -> void


trait IntStorage uses Storage[int]:
	pass


trait StringStorage uses Storage[String]:
	pass


class Bad uses IntStorage, StringStorage:
	func add(item: int) -> void:
		pass


func test() -> void:
	pass
