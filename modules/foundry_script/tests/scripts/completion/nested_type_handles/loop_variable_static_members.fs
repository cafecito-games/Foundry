trait Factory extends RefCounted:
	abstract static func create() -> Self

	abstract func describe() -> String


var handles: Array[Type[Factory]] = []


func build_all() -> void:
	for handle in handles:
		handle.➡
