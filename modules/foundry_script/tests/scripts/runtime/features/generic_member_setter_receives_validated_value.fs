# The class-parameter check converts where a member store would, and it runs before the setter, so a
# `T`-typed member's setter observes exactly the value a concretely typed member's setter does: the
# `int` member's setter parameter converts `1.5` through ordinary argument checking, and the erased
# `T` member reaches the same value through the receiver-relative check on the write.
class Box[T]:
	var value: T:
		set(incoming):
			prints("value setter", incoming)
			value = incoming

	var count: int:
		set(incoming):
			prints("count setter", incoming)
			count = incoming

	func put(supplied) -> void:
		value = supplied

	func tally(supplied) -> void:
		count = supplied

	func bump(supplied) -> void:
		value += supplied

	func tally_bump(supplied) -> void:
		count += supplied


func test() -> void:
	var box := Box[int].new()
	box.put(1.5)
	box.tally(1.5)
	Utils.check(box.value == 1)
	Utils.check(box.count == 1)
	box.bump(0.5)
	box.tally_bump(0.5)
	Utils.check(box.value == 1)
	Utils.check(box.count == 1)
	print("generic member setter receives validated value ok")
