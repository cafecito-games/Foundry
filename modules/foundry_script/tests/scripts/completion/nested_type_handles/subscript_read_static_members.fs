trait Factory extends RefCounted:
	abstract static func create() -> Self

	abstract func describe() -> String


var types: Dictionary[String, Type[Factory]] = {}


func build(name: String) -> Factory:
	var factory: Type[Factory] = types[name]
	return factory.➡
