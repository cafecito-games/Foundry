# The element type of a container-shaped generic argument keeps the handle layer at runtime, so an
# instance cannot be appended through an untyped alias.
trait Factory extends RefCounted:
	abstract static func create() -> Self


class User extends RefCounted:
	uses Factory

	static func create() -> User:
		return User.new()


class Slot[T]:
	var value: T


func test() -> void:
	var slot := Slot[Array[Type[Factory]]].new()
	slot.value = [User]
	var untyped: Array = slot.value
	untyped.append(User.new())
	print(slot.value.size())
