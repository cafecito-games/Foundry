# Every static boundary asks the same membership question, so contradicting trait arguments are
# rejected at a parameter, at a return, and at a member write, not only at a variable declaration.
trait Keeper[T]:
	func label() -> String:
		return "keeper"


class ForwardingKeeper[U]:
	uses Keeper[U]


class Holder:
	var kept: Keeper[int]


func take(value: Keeper[int]) -> void:
	print(value.label())


func give() -> Keeper[int]:
	return ForwardingKeeper[String].new()


func test() -> void:
	take(ForwardingKeeper[String].new())
	var holder := Holder.new()
	holder.kept = ForwardingKeeper[String].new()
	print(give())
