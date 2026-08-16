# A specialized class handle whose arguments name a class type parameter cannot be folded to a
# compile-time constant: one constant would have to stand for every specialization. It is built at run
# time from the receiver instead, so constructing through the aliased handle reifies exactly as the
# direct `Holder[Type[U]].new()` form does. A handle whose arguments are all concrete keeps folding to
# a constant.
class Holder[T: Type[Node]]:
	var value: T


class Wrapper[U: Node]:
	func make_via_alias() -> Holder[Type[U]]:
		var handle := Holder[Type[U]]
		return handle.new()

	func make_via_concrete_alias() -> Holder[Type[Button]]:
		var handle := Holder[Type[Button]]
		return handle.new()


func test() -> void:
	var wrapper := Wrapper[Button].new()
	var made: Variant = wrapper.make_via_alias()
	print(made is Holder[Type[Button]])
	print(made is Holder[Type[Label]])
	made.value = Button
	print(made.value == Button)

	var concrete: Variant = wrapper.make_via_concrete_alias()
	print(concrete is Holder[Type[Button]])
	print("specialized handle alias reified ok")
