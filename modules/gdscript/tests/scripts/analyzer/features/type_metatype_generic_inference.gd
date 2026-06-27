trait Creatable:
	abstract func label() -> String


class User:
	uses Creatable

	func label() -> String:
		return "user"


func accept[T](_klass: Type[T], value: T) -> T:
	return value


func id_type[T](t: Type[T]) -> Type[T]:
	return t


func accept_creatable[T: Creatable](_klass: Type[T]) -> void:
	pass


func make[T: Creatable](_klass: Type[T]) -> T:
	return null


func test():
	var klass: Type[Node] = Node
	var node := Node.new()
	var inferred: Node = accept(klass, node)
	var inferred_user_handle: Type[User] = id_type(User)
	var inferred_node_handle: Type[Node] = id_type(Node)
	var made: User = make(User)
	accept_creatable(User)
	print(inferred == node)
	print(inferred_user_handle == User)
	print(inferred_node_handle == Node)
	print(made == null)
	node.free()
