# Two engine classes on one ClassDB chain bind the same trait to different type arguments. Membership
# resolves nearest-conformance-first, so widening a `RefCounted` to `Object` silently switches which
# arguments apply: `RnccKeeper[int].make()` is statically an `int` and dispatches the `String` witness.
trait RnccKeeper[T]:
	abstract func make() -> T


extend Object uses RnccKeeper[int]:
	func make() -> int:
		return 7


extend RefCounted uses RnccKeeper[String]:
	func make() -> String:
		return "seven"


func test() -> void:
	var value: Object = RefCounted.new()
	var keeper: RnccKeeper[int] = value
	print(keeper.make())
