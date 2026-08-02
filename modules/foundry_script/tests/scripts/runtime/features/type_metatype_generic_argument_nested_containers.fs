# `Type[T]` survives a generic argument that is itself a container, so `Slot[Array[Type[Factory]]]`
# and `Catalog[Dictionary[String, Type[Factory]]]` declare, construct, and read as handle collections.
trait Factory extends RefCounted:
	abstract static func create() -> Self


class User extends RefCounted:
	uses Factory

	static func create() -> User:
		return User.new()


class Slot[T]:
	var value: T


class Catalog[T]:
	var entries: T


func test() -> void:
	var slot: Slot[Array[Type[Factory]]] = Slot[Array[Type[Factory]]].new()
	slot.value = [User]
	slot.value.append(User)
	print(slot.value.size())
	print(slot.value[0].create() is User)

	var catalog: Catalog[Dictionary[String, Type[Factory]]] = Catalog[Dictionary[String, Type[Factory]]].new()
	catalog.entries = { "user": User }
	print(catalog.entries["user"].create() is User)
	print("nested container arguments ok")
