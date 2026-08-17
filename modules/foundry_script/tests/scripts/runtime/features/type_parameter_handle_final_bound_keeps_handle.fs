# A bound written as `Type[...]` denotes a class handle, and a parameter bounded by one denotes that
# handle even where the parameter itself is spelled bare. Resolving such a bound to the class it names
# has to keep the handle layer: without it the slot would demand an instance of the final class and
# reject the very handle the declaration exists for.
final class HfbFactory:
	func read() -> String:
		return "factory"


func keep[T: Type[HfbFactory]](factory: T) -> bool:
	var kept: T = factory
	return kept == HfbFactory


func nested[T: Type[HfbFactory]](factory: T) -> bool:
	var kept: Array[T] = [factory]
	return kept[0] == HfbFactory


func supply() -> Variant:
	return HfbFactory


func test() -> void:
	print(keep[Type[HfbFactory]](HfbFactory))
	print(nested[Type[HfbFactory]](HfbFactory))
	print(keep[Type[HfbFactory]](supply()))
