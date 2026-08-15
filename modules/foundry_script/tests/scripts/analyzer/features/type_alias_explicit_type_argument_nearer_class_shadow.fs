# A nearer class named the same as a farther-out file-scope alias shadows the alias in explicit
# type-argument position, the same as ordinary identifier shadowing: the alias lookup must stop at
# the first scope that claims the name for anything, not skip past a non-alias declaration to keep
# searching outward for an alias.
type Shadowed = int


class Outer:
	class Shadowed:
		var label: String = "inner"

	class Holder[T]:
		var value: T

	func test_argument() -> String:
		var holder := Holder[Shadowed].new()
		holder.value = Shadowed.new()
		return holder.value.label


func test():
	print(Outer.new().test_argument())
