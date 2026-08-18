# The chain rule compares composite arguments component by component, so two declarations that agree
# on the container and disagree on its element are still a contradiction.
trait RncccKeeper[T]:
	abstract func make() -> T


extend Object uses RncccKeeper[Array[int]]:
	func make() -> Array[int]:
		return [7]


extend RefCounted uses RncccKeeper[Array[String]]:
	func make() -> Array[String]:
		return ["seven"]


func test() -> void:
	print("unreachable")
