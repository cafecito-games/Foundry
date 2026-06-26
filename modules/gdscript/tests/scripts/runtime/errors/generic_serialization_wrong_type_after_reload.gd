# After a reified instance round-trips through resource duplication (the same storage path as
# `.tres`/scene save-load), a wrong-typed write into its `T`-typed member is still rejected at
# runtime, just as on a freshly constructed `Box[int]`.
class Box[T]:
	extends Resource
	var value: T

	func put(v) -> void:
		value = v


func test() -> void:
	var box := Box[int].new()
	var clone := box.duplicate() as Box
	clone.put("not an int")
	print(clone.value)
