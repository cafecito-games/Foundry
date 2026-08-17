# The rejecting half of the specialized-local store. Each rejection lives in its own function because a
# rejected store ends the function it happens in, and the diagnostic names the declared slot with its
# arguments so `Pair[int, String]` is distinguishable from `Pair`.
class Pair[A, B]:
	pass


class ConflictingPair extends Pair[int, Node]:
	pass


trait Holder[T]:
	func label() -> String:
		return "holder"


class IntHolder:
	uses Holder[int]


class StringHolder:
	uses Holder[String]


class Plain:
	pass


func supply(value: Variant) -> Variant:
	return value


func reject_conflicting_arguments() -> void:
	var _slot: Pair[int, String] = supply(Pair[int, Node].new())
	print("not reached: conflicting arguments")


func reject_conflicting_projection() -> void:
	var _slot: Pair[int, String] = supply(ConflictingPair.new())
	print("not reached: conflicting projection")


func reject_unrelated_class() -> void:
	var _slot: Pair[int, String] = supply(Plain.new())
	print("not reached: unrelated class")


func reject_conflicting_trait_arguments() -> void:
	var _slot: Holder[int] = supply(StringHolder.new())
	print("not reached: conflicting trait arguments")


func reject_nonconformer() -> void:
	var _slot: Holder[int] = supply(Plain.new())
	print("not reached: nonconformer")


func test() -> void:
	print("accepted: ", supply(IntHolder.new()) != null)
	reject_conflicting_arguments()
	reject_conflicting_projection()
	reject_unrelated_class()
	reject_conflicting_trait_arguments()
	reject_nonconformer()
	print("done")
