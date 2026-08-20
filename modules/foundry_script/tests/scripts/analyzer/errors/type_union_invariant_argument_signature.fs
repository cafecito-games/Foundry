# An invariant type argument decides identity component by component, but a union was compared in one
# step through `DataType::operator==`, which never looks at a callable's signature. Two unions that
# differ only in an alternative's signature then read as one type, and a `Keeper` of one is admitted
# into a slot declared as the other -- where the callee would invoke the stored callback with the
# wrong argument type.
class Keeper[T]:
	var held: T


func take(k: Keeper[Callable[[int], void] | int]) -> void:
	print("took ", k)


func relay(k: Keeper[Callable[[String], void] | int]) -> void:
	take(k)


func test() -> void:
	print("skipped")
