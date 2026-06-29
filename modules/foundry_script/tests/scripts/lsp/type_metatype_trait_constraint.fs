trait Creatable extends RefCounted:
	abstract static func create() -> Self

class User extends RefCounted:
	uses Creatable

	static func create() -> User:
		return User.new()

func factory[T: Creatable](creatable_type: Type[T]) -> T:
	return creatable_type.create()

func testing() -> void:
	var user_type: Type[User] = User
	user_type.create()
