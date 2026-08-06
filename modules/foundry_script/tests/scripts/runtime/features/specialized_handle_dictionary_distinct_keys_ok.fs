# A specialized handle and an unrelated bare script never collide when erased into the same
# `Dictionary[Script, int]` slot: only two different specializations of the *same* script share an
# erased identity. Both entries must survive the merge.
class Box[T]:
	pass


class Other:
	pass


func test() -> void:
	var registry: Dictionary[Script, int] = {}
	var source: Dictionary = {}
	source[Box[int]] = 1
	source[Other] = 2
	registry.merge(source)
	print("registry size %d" % registry.size())
