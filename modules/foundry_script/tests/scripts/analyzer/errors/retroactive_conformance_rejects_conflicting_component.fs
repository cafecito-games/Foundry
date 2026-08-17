# A composite recorded argument keeps its components, so a conformance that recorded `Array[int]`
# contradicts a destination declaring `Array[String]` even though the composite as a whole has no
# single flattened identity.
trait RclKeeper[T]:
	abstract func keep(item: T) -> T


class RclTarget:
	pass


extend RclTarget uses RclKeeper[Array[int]]:
	func keep(item: Array[int]) -> Array[int]:
		return item


func test() -> void:
	var erased: RclKeeper[Array[String]] = RclTarget.new()
	print(erased != null)
