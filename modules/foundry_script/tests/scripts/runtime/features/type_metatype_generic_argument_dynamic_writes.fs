# The reified argument of `Slot[Type[Factory]]` is a class-handle descriptor, so a dynamic
# (`Variant`-typed) write goes through the same class-handle rule as an analyzed one: conforming
# handles, including a subclass handle, are accepted.
trait Factory extends RefCounted:
	abstract static func create() -> Self


class User extends RefCounted:
	uses Factory

	static func create() -> User:
		return User.new()


class Admin extends User:
	static func create() -> Admin:
		return Admin.new()


class Slot[T]:
	var value: T


func test() -> void:
	var slot := Slot[Type[Factory]].new()
	var dynamic: Variant = slot
	dynamic.value = User
	print(slot.value == User)
	dynamic.value = Admin
	print(slot.value == Admin)
	print("dynamic handle writes ok")
