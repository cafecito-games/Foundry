# A trait applied with a class-handle argument (`Slotted[Type[Factory]]`) reifies the handle layer,
# so a dynamic write of an instance rather than a class handle is rejected at runtime.
trait Factory extends RefCounted:
	abstract static func create() -> Self


class User extends RefCounted:
	uses Factory

	static func create() -> User:
		return User.new()


trait Slotted[T]:
	var slot: T


class Holder:
	uses Slotted[Type[Factory]]


func test() -> void:
	var holder := Holder.new()
	holder.slot = User
	var dynamic: Variant = holder
	dynamic.slot = User.new()
	print(holder.slot == User)
