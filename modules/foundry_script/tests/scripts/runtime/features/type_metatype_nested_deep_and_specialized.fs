trait Factory extends RefCounted:
	abstract static func create() -> Self


class User extends RefCounted:
	uses Factory

	static func create() -> User:
		return User.new()


class Box[T] extends RefCounted:
	var value: T


func test():
	# Nesting deeper than one level declares, writes, and reads.
	var nested: Array[Dictionary[String, Type[Factory]]] = [{ "user": User }]
	nested.append({ "second": User })
	print(nested.size())
	print(nested[0]["user"].create() is User)

	# A specialized handle keeps its type arguments in the element slot.
	var boxes: Array[Type[Box[int]]] = [Box[int]]
	print(boxes.size())
	print(boxes[0] == Box[int])

	# A handle-typed dictionary key round-trips.
	var by_class: Dictionary[Type[Node], String] = { Node: "node" }
	by_class[Button] = "button"
	print(by_class[Node])
	print(by_class[Button])
