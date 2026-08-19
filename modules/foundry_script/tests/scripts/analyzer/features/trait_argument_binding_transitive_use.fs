# Applying a non-generic trait that binds a generic supertrait at its own declaration site needs no
# restating of the arguments: `uses Wrapper` alone carries `Storage[int]` into the implementer.
trait Storage[T]:
	func stored(value: T) -> T:
		return value


trait Wrapper:
	uses Storage[int]


class C:
	uses Wrapper


func test() -> void:
	print(C.new().stored(1))
