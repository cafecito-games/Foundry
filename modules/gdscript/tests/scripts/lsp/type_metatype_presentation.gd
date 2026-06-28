trait Creatable:
	static func create() -> Self

class User:
	uses Creatable

	static func create() -> User:
		return User.new()

class Box[T]:
	var value: T

var user_type: Type[User] = User
var node_type: Type[Node] = Node
var int_box_type: Type[Box[int]] = Box[int]

func factory[T: Creatable](factory_type: Type[T]) -> T:
	return factory_type.create()

func use_factory() -> User:
	return factory(User)
