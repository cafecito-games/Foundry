# `assign()` rebuilds the whole destination from the source, so it collapses the same two
# specializations `merge()` does and gets the same rejection.
class Box[T]:
	pass


func test() -> void:
	var registry: Dictionary[Script, int] = {}
	var source: Dictionary = {}
	source[Box[int]] = 1
	source[Box[String]] = 2
	registry.assign(source)
	print("not ok")
