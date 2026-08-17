# A source typed as exactly the engine class the conformance names still permits a downcast to a
# conforming subclass, so the store keeps the gradual rule and takes a run-time check. Before the
# recorded arguments were compared, no check was emitted at all and the contradiction went unnoticed.
trait RcnKeeper[T]:
	abstract func keep(item: T) -> T


extend RefCounted uses RcnKeeper[int]:
	func keep(item: int) -> int:
		return item


func test() -> void:
	var value := RefCounted.new()
	var conflicting: RcnKeeper[String] = value
	print(conflicting)
