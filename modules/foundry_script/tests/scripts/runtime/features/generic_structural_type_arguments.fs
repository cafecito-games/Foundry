# A structural type argument — an unnamed tuple type or a callable signature — is spelled the same way
# in a type annotation and in a value-position application, so both positions carry the same
# specialization and the payloads keep their concrete element types at runtime.
class Box[T]:
	var value: T

	func _init(initial: T) -> void:
		value = initial


enum Holder[T]:
	Some(value: T)
	None


func describe(holder: Holder[(int, String)]) -> String:
	match holder:
		Holder[(int, String)].Some(var pair):
			return "some %d %s" % [pair[0], pair[1]]
		Holder[(int, String)].None:
			return "none"
	return "unreachable"


func test() -> void:
	var pair: Box[(int, String)] = Box[(int, String)].new((1, "x"))
	print(pair.value[0])
	print(pair.value[1])

	var callback: Box[Callable[[int], bool]] = Box[Callable[[int], bool]].new(func(n: int) -> bool: return n > 0)
	print(callback.value.call(3))
	print(callback.value.call(-3))

	print(describe(Holder[(int, String)].Some((2, "y"))))
	print(describe(Holder[(int, String)].None))
	print("structural type arguments ok")
