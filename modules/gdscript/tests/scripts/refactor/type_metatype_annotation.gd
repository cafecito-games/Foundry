trait Creatable:
	static func create() -> Self

class User:
	uses Creatable

	static func create() -> User:
		return User.new()

var user_type = User
var node_type = Node
