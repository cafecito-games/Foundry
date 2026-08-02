# A dynamic write of a handle whose represented class does not conform to the trait is rejected at
# runtime, so the analyzer is not the only gate on a nested `Type[T]` argument.
trait Factory extends RefCounted:
	abstract static func create() -> Self


class User extends RefCounted:
	uses Factory

	static func create() -> User:
		return User.new()


class Unrelated extends RefCounted:
	pass


class Slot[T]:
	var value: T


func test() -> void:
	var slot := Slot[Type[Factory]].new()
	slot.value = User
	var dynamic: Variant = slot
	dynamic.value = Unrelated
	print(slot.value == User)
