# A member typed with a class-dependent specialized handle is checked against the receiver once the
# construction reifies: a `Wrapper[Button]` rejects a `Holder[Type[Label]]`, which before reification
# was indistinguishable from the value the class builds for itself.
class Holder[T: Type[Node]]:
	var value: T


class Wrapper[U: Node]:
	var holder: Holder[Type[U]] = Holder[Type[U]].new()


func test() -> void:
	var wrapper := Wrapper[Button].new()
	var dynamic: Variant = wrapper
	dynamic.holder = Holder[Type[Label]].new()
	print("unreached")
