# Two engine classes on one ClassDB chain may conform to the same generic trait as long as they bind
# it to the same type arguments; the nearest conformance still wins for dispatch. The static type of
# each call and the value it produces agree, which is exactly what the conflicting form cannot offer.
trait RnccmKeeper[T]:
	abstract func make() -> T


extend Object uses RnccmKeeper[int]:
	func make() -> int:
		return 7


extend RefCounted uses RnccmKeeper[int]:
	func make() -> int:
		return 11


func test() -> void:
	var value: Object = RefCounted.new()
	var keeper: RnccmKeeper[int] = value
	var produced: int = keeper.make()
	print(produced + 1)
