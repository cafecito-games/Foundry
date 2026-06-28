trait Makeable:
	abstract static func make() -> Self


class Box[T]:
	uses Makeable

	static func make() -> Box[String]:
		return Box[String].new()


func test() -> void:
	pass
