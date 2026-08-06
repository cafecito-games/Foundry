# A conformance declared on an engine class answers for its subclasses: the ancestor walk that
# supplies membership supplies the declared arguments from the same nearest conforming ancestor.
trait NativeStore[T]:
	abstract func store(item: T) -> void

	abstract func fetch() -> T


extend RefCounted uses NativeStore[int]:
	func store(_item: int) -> void:
		pass

	func fetch() -> int:
		return 0


func test() -> void:
	# `Resource` extends `RefCounted`, so it reaches the conformance through the ancestor walk.
	var resource: Variant = Resource.new()

	print("resource is NativeStore: ", resource is NativeStore)
	print("resource is NativeStore[int]: ", resource is NativeStore[int])
	print("resource is NativeStore[String]: ", resource is NativeStore[String])

	var exact_cast: Variant = resource as NativeStore[int]
	print("exact cast keeps identity: ", exact_cast == resource)
	var mismatched_cast: Variant = resource as NativeStore[String]
	print("mismatched cast is null: ", mismatched_cast == null)
