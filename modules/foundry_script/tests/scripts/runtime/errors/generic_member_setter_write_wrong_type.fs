# A setter on a `T`-typed member runs only after the write has been validated against the receiver's
# reified argument, so a rejected value produces no setter side effect at all and the failure is
# reported at the storing assignment instead of inside the setter's write-back.
class Box[T]:
	var seen: Array = []
	var value: T:
		set(incoming):
			print("value setter ran")
			seen.append(incoming)
			value = incoming

	func put(supplied) -> void:
		value = supplied


func test() -> void:
	var box := Box[int].new()
	box.put("not an int")
	print(box.seen)
	print(box.value)
