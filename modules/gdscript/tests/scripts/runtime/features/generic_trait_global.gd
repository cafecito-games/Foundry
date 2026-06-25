# A globally-declared generic trait (`trait_name GenericStore[T]`) is applied with a concrete type
# argument from another file, and its substituted requirements are satisfied at runtime.
extends RefCounted
uses GenericStore[int]

var value: int = 0


func store(item: int) -> void:
	value = item


func fetch() -> int:
	return value


func test() -> void:
	store(42)
	print(fetch())
	print(self is GenericStore)
