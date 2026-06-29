trait Creatable:
	abstract func label() -> String


class Plain:
	pass


func make[T: Creatable](_klass: Type[T]) -> void:
	pass


func test():
	make(Plain)
