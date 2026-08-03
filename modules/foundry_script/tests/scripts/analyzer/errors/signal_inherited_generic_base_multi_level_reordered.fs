# Type arguments are forwarded and reordered through every `extends` edge, and substitution reaches
# inside a composite parameter: through `Middle[X, Y] extends Base[Y, X]` and
# `Child extends Middle[String, int]`, `Base.reported` is observed as `Signal[[int, Array[String]]]`.
class Base[A, B]:
	signal reported(first: A, second: Array[B])


class Middle[X, Y] extends Base[Y, X]:
	pass


class Child extends Middle[String, int]:
	pass


func test(child: Child) -> void:
	child.reported.emit(1, ["ok"])
	child.reported.emit("bad", [1])
