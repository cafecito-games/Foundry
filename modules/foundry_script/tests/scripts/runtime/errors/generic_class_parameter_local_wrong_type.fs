# A local declared with a class type parameter is checked against the receiver's reified argument, so
# a value no `Crate[int]` could hold fails at the store rather than travelling on untested.
class Crate[T]:
	func keep(value) -> T:
		var kept: T = value
		return kept


func test() -> void:
	var crate := Crate[int].new()
	print(crate.keep("not an int"))
