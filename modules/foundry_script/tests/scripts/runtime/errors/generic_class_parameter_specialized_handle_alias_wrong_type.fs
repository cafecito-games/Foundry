# The aliased-handle construction path reifies the same way, so a value built through a differently
# specialized alias is rejected at the slot just as a directly constructed one is.
class Holder[T: Type[Node]]:
	var value: T


class Wrapper[U: Node]:
	func keep(value):
		var kept: Holder[Type[U]] = value
		return kept

	func make_wrong():
		var handle := Holder[Type[Label]]
		return handle.new()


func test() -> void:
	var wrapper := Wrapper[Button].new()
	print(wrapper.keep(wrapper.make_wrong()))
