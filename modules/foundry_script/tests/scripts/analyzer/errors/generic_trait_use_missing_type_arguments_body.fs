# A `uses` entry in a class body naming a generic trait must spell its type arguments. The entry
# still applies after the error, so the trait's members stay reachable and no cascade follows.
trait Storage[T]:
	func stored(value: T) -> T:
		return value


class C:
	uses Storage


func test() -> void:
	var c := C.new()
	print(c.stored(1))
