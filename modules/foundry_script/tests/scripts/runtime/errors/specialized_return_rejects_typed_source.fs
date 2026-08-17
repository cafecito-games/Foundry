# The rejecting half of the specialized return with a statically typed returned value. A source typed
# as an unspecialized base is assignable to a specialized return, so the only thing that can decide the
# arguments is the evidence the value carries, and evidence that contradicts the declaration rejects.
# The diagnostic names both sides with their arguments.
class Pair[A, B]:
	pass


class ConflictingPair extends Pair[int, Node]:
	pass


trait Holder[T]:
	func label() -> String:
		return "holder"


class StringHolder:
	uses Holder[String]


func supply(value: Variant) -> Variant:
	return value


func reject_conflicting_arguments() -> Pair[int, String]:
	var value: Pair = supply(Pair[int, Node].new())
	return value


func reject_conflicting_projection() -> Pair[int, String]:
	var value: Pair = supply(ConflictingPair.new())
	return value


func reject_conflicting_trait_arguments() -> Holder[int]:
	var value: Holder = supply(StringHolder.new())
	return value


func test() -> void:
	print("conflicting arguments: ", reject_conflicting_arguments() == null)
	print("conflicting projection: ", reject_conflicting_projection() == null)
	print("conflicting trait arguments: ", reject_conflicting_trait_arguments() == null)
	print("done")
