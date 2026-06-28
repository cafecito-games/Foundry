trait Creatable:
	abstract func label() -> String

class User:
	uses Creatable

	func label() -> String:
		return "user"

class Box[T]:
	var value: T

var user_type: Type[User] = User
var node_type: Type[Node] = Node
var int_box_type: Type[Box[int]] = Box[int]

func accept[T](klass: Type[T], value: T) -> T:
	return value

func id_type[T](t: Type[T]) -> Type[T]:
	return t

func use_handles() -> void:
	var inferred_user: Type[User] = id_type(User)
	accept(User, User.new())
