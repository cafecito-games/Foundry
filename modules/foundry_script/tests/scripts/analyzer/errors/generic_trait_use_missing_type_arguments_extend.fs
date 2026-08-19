# A retroactive conformance naming a generic trait must spell its type arguments, exactly as an
# ordinary `uses` entry must.
trait Storage[T]:
	abstract func stored(value: T) -> T


class C extends RefCounted:
	pass


extend C uses Storage:
	func stored(value: int) -> int:
		return value


func test() -> void:
	pass
