# Correctly typed use of a signal inherited from a generic base: the parameters are specialized
# against the receiver's type arguments through one level, through a reordering multi-level chain,
# and through the string-name `Object` APIs. Issue #1699.
func on_reported(value: int) -> void:
	print("reported: ", value)


func on_pair(first: int, second: Array[String]) -> void:
	print("pair: ", first, " ", second)


func test() -> void:
	var child := Child.new()
	child.reported.connect(on_reported)
	child.go()
	child.emit_signal("reported", 2)

	var direct := Base[int].new()
	direct.reported.connect(on_reported)
	direct.reported.emit(3)

	var nested := Nested.new()
	nested.reported.connect(on_pair)
	nested.reported.emit(4, ["ok"])


class Base[T]:
	signal reported(value: T)


class Child extends Base[int]:
	func go() -> void:
		reported.emit(1)


class Pair[A, B]:
	signal reported(first: A, second: Array[B])


class Middle[X, Y] extends Pair[Y, X]:
	pass


class Nested extends Middle[String, int]:
	pass
