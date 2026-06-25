# A generic trait `Holder[T]` is applied with a concrete type argument (`uses Holder[int]`).
# Conformance is checked under substitution: the implementation must satisfy every required
# member with `T := int` applied. The parameterized trait also works as a plain type in `is`/`as`
# checks and in typed parameter annotations.
trait Holder[T]:
	@abstract func add(item: T) -> void
	@abstract func first() -> T


class IntBox uses Holder[int]:
	var items: Array[int] = []

	func add(item: int) -> void:
		items.append(item)

	func first() -> int:
		return items[0]


func take_holder(holder: Holder) -> bool:
	return holder != null


func test() -> void:
	var box := IntBox.new()
	box.add(7)
	print(box.first())
	print(box is Holder)
	var as_holder := box as Holder
	print(as_holder != null)
	print(take_holder(box))
	print("generic trait conformance ok")
