# A `Type[T]` generic class argument keeps its class-handle layer: `Slot[Type[Factory]]` stores class
# handles, and reading the member back yields `Type[Factory]` rather than an instance of `Factory`.
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


func describe(slot: Slot[Type[Factory]]) -> bool:
	# Passing the specialization as an argument keeps the handle layer on the member read.
	return slot.value.create() is Admin


func test() -> void:
	var slot: Slot[Type[Factory]] = Slot[Type[Factory]].new()
	slot.value = User
	var factory: Type[Factory] = slot.value
	var product: Factory = factory.create()
	print(product is User)

	# A static member promised by the trait is callable straight off the read, without a cast.
	print(slot.value.create() is User)

	# A conforming subclass handle is accepted.
	slot.value = Admin
	print(slot.value.create() is Admin)

	# Aliasing the specialization keeps the reified handle argument.
	var aliased := slot
	print(aliased.value == Admin)
	print(describe(slot))
	print("generic argument handle ok")
