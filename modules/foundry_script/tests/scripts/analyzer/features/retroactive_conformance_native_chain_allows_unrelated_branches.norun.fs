# `RefCounted` and `Node` are both direct `Object` subclasses but neither derives from the other, so
# no value reaches both conformances and the two bindings never describe the same receiver. Compiled
# but not run: instantiating a `Node` here would leak it.
trait RnccuKeeper[T]:
	abstract func make() -> T


extend RefCounted uses RnccuKeeper[int]:
	func make() -> int:
		return 7


extend Node uses RnccuKeeper[String]:
	func make() -> String:
		return "seven"


func test() -> void:
	var value := RefCounted.new()
	var keeper: RnccuKeeper[int] = value
	print(keeper.make())
