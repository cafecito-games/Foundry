# A class type parameter used as the type argument of a specialized class handle is reified at the
# construction site against the running receiver, so `Holder[Type[U]].new()` inside `Wrapper[Button]`
# builds a `Holder[Type[Button]]` rather than the erased handle the body's single compilation would
# otherwise describe. The body is still compiled once for the declaring class; only the arguments the
# instruction carries stay unresolved until the frame supplies its receiver.
class Holder[T: Type[Node]]:
	var value: T


class Wrapper[U: Node]:
	var holder: Holder[Type[U]] = Holder[Type[U]].new()

	func build() -> Holder[Type[U]]:
		return Holder[Type[U]].new()


func test() -> void:
	var wrapper := Wrapper[Button].new()
	var from_member: Variant = wrapper.holder
	print(from_member is Holder[Type[Button]])
	print(from_member is Holder[Type[Label]])

	var from_method: Variant = wrapper.build()
	print(from_method is Holder[Type[Button]])

	# The reified slot accepts the receiver's own argument, and the member accepts an equally
	# specialized value through an untyped hop.
	from_member.value = Button
	print(wrapper.holder.value == Button)
	var dynamic: Variant = wrapper
	dynamic.holder = wrapper.build()
	print(wrapper.holder.value == null)
	print("specialized handle argument reified ok")
