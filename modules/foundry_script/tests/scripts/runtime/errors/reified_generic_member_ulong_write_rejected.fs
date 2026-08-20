# `ulong` -> `long` keeps requiring an explicit cast on a reified generic member: only a value the
# `uint` range contains crosses carriers, so a `ulong`-magnitude value is rejected even though it
# would fit `long`, and the slot keeps what it already held.
class Box[T]:
	var value: T


func test() -> void:
	var box := Box[long].new()
	var dynamic: Variant = box
	dynamic.value = 7
	print(box.value)
	var big: ulong = 5000000000
	dynamic.value = big
	print("unreachable")
