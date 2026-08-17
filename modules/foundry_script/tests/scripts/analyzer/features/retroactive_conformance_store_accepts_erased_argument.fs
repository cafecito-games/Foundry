# A composite argument has no flattened identity the recorded form can compare with certainty, so it
# contributes no evidence for its whole position and the store stays accepted.
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
