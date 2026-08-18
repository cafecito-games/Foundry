# A script class and the engine class its chain ends on may both conform to one generic trait as long
# as they bind it to the same type arguments. The nearest conformance still wins for dispatch, so the
# narrow view and the widened view of one value answer with the same witness and the same type.
trait SchmKeeper[T]:
	abstract func make() -> T


class SchmHolder extends RefCounted:
	pass


extend RefCounted uses SchmKeeper[int]:
	func make() -> int:
		return 7


extend SchmHolder uses SchmKeeper[int]:
	func make() -> int:
		return 11


func test() -> void:
	var holder := SchmHolder.new()
	var narrow: SchmKeeper[int] = holder
	var widened: RefCounted = holder
	var wide: SchmKeeper[int] = widened
	var produced: int = narrow.make()
	var also_produced: int = wide.make()
	print(produced)
	print(also_produced)
