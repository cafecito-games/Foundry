# A class conformed only through `extend Target uses Trait[...]` has no declared `uses` to project,
# so its evidence lives in the conformance registry. It is compared there just the same.
trait RccKeeper[T]:
	abstract func keep(item: T) -> T


class RccTarget:
	pass


extend RccTarget uses RccKeeper[int]:
	func keep(item: int) -> int:
		return item


func test() -> void:
	var value := RccTarget.new()
	var conflicting: RccKeeper[String] = value
	print(conflicting)
