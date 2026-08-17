# Recorded evidence that agrees with the destination is accepted, for every conformer kind. This is
# the guard against over-rejection: the comparison only rejects a genuine contradiction.
trait RckKeeper[T]:
	abstract func keep(item: T) -> T


class RckTarget:
	pass


extend RckTarget uses RckKeeper[int]:
	func keep(item: int) -> int:
		return item


extend RefCounted uses RckKeeper[int]:
	func keep(item: int) -> int:
		return item


extend int uses RckKeeper[int]:
	func keep(item: int) -> int:
		return item


func test() -> void:
	var from_class: RckKeeper[int] = RckTarget.new()
	var from_native: RckKeeper[int] = RefCounted.new()
	var number := 3
	var from_builtin: RckKeeper[int] = number
	print(from_class != null, from_native != null, from_builtin != null)
