trait Factory extends RefCounted:
	abstract static func create() -> Self

class User extends RefCounted:
	uses Factory

	static func create() -> User:
		return User.new()

class Slot[T]:
	var value: T

signal registered(name: String, factory: Type[Factory])

var types: Dictionary[String, Type[Factory]] = {}
var handles: Array[Type[Factory]] = []
var grouped: Array[Dictionary[String, Type[Factory]]] = []
var slot: Slot[Type[Factory]] = Slot[Type[Factory]].new()
var construct: Callable[[Type[Factory]], Factory]

func register(name: String, factory: Type[Factory]) -> Type[Factory]:
	types[name] = factory
	return factory

func build(name: String) -> Factory:
	var factory: Type[Factory] = types[name]
	return factory.create()

func build_all() -> void:
	for handle in handles:
		handle.create()

func use_handles() -> void:
	register("user", User)
