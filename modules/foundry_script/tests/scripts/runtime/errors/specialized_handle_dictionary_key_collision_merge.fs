# Two different specializations of the same script (`Box[int]`, `Box[String]`) both erase to the bare
# `Box` script once they land in a `Dictionary[Script, int]` slot, so `merge()` would otherwise silently
# collapse two source entries into one. Erasure rejects the write instead of dropping an entry.
class Box[T]:
	pass


func test() -> void:
	var registry: Dictionary[Script, int] = {}
	var source: Dictionary = {}
	source[Box[int]] = 1
	source[Box[String]] = 2
	print("source size %d" % source.size())
	registry.merge(source)
	print("not ok")
