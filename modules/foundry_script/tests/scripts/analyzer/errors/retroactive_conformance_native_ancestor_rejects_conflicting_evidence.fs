# A conformance declared on an engine class records its arguments for every subclass that reaches it
# through the ancestor walk, so a store from a subclass-typed source is checked against them.
trait RcnaKeeper[T]:
	abstract func keep(item: T) -> T


extend RefCounted uses RcnaKeeper[int]:
	func keep(item: int) -> int:
		return item


func test() -> void:
	var value: Resource = Resource.new()
	var conflicting: RcnaKeeper[String] = value
	print(conflicting)
