# Policy for a generic class named without type arguments (a "raw" generic, `var b: Box`): it
# behaves like an untyped `Array`/`Dictionary`. It is a permissive supertype — any specialization
# `Box[X]` assigns to it — and its members are dynamic (the unbound type parameter is leniently
# compatible with concrete types, like Variant), with writes validated against the instance's
# reified type argument at runtime.
class Box[T]:
	var value: T

	func get_value() -> T:
		return value


func test() -> void:
	# Permissive supertype: a raw `Box` accepts any specialization.
	var b: Box = Box[int].new()
	b.value = 42
	print(b.get_value())

	# Dynamic members: the raw member flows into a typed binding with only a runtime check, like an
	# untyped collection element.
	var s: String = b.value
	print(typeof(s) == TYPE_INT)

	# A different specialization also assigns to the same raw type.
	b = Box[String].new()
	b.value = "hi"
	print(b.get_value())
	print("raw generic ok")
