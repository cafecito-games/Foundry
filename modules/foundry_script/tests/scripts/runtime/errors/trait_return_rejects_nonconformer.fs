# The trait half of the return boundary rejects on the two things a trait type can disagree about:
# a value that conforms to no such trait at all, and a conformer whose recorded arguments contradict
# the declared specialization. The diagnostic names the declared trait with its arguments.
trait Keeper[T]:
	func label() -> String:
		return "keeper"


class IntKeeper:
	uses Keeper[int]


class StringKeeper:
	uses Keeper[String]


class Plain:
	pass


class Source:
	func give(value: Variant) -> Keeper[int]:
		return value


func test() -> void:
	var source := Source.new()
	print(source.give(IntKeeper.new()) != null)
	print(source.give(StringKeeper.new()) != null)
	print(source.give(Plain.new()) != null)
	print(source.give("text") != null)
