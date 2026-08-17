# A retroactively conformed builtin records the arguments its conformance supplied, so a specialized
# store that contradicts them is rejected statically rather than accepted unchecked.
trait RcbKeeper[T]:
	abstract func keep(item: T) -> T


extend int uses RcbKeeper[int]:
	func keep(item: int) -> int:
		return item


func test() -> void:
	var value := 3
	var conflicting: RcbKeeper[String] = value
	print(conflicting)
