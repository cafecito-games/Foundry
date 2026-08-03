class User:
	pass

class Slot[T]:
	var value: T

enum Kind:
	User = 0

signal registered(name: String, factory: Type[User])

var handles: Array[Type[User]] = []
var registry: Dictionary[String, Type[User]] = {}
var grouped: Array[Dictionary[String, Type[User]]] = []
var slot: Slot[Type[User]] = Slot[Type[User]].new()
var construct: Callable[[Type[User]], User]
var kind: Kind = Kind.User

func lookup(name: String) -> Type[User]:
	return registry[name]
