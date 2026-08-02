# A dynamic write of an instance into a `Slot[Type[Factory]]` member is rejected at runtime: the
# reified argument denotes the class, not values of it.
trait Factory extends RefCounted:
	abstract static func create() -> Self


class User extends RefCounted:
	uses Factory

	static func create() -> User:
		return User.new()


class Slot[T]:
	var value: T


func test() -> void:
	var slot := Slot[Type[Factory]].new()
	slot.value = User
	var dynamic: Variant = slot
	dynamic.value = User.new()
	print(slot.value == User)
