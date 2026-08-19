# An argument-less entry naming a generic trait is an error even when the same trait is bound with
# arguments through another entry on the same class — via a supertrait or a sibling entry.
trait Storage[T]:
	func stored(value: T) -> T:
		return value


trait Wrapper:
	uses Storage[int]


class BoundThroughSupertrait:
	uses Storage, Wrapper


class BoundBySiblingEntry:
	uses Storage, Storage[int]


func test() -> void:
	pass
