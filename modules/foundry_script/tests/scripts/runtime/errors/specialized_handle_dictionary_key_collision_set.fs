# The single-key `set()` path collapses the same way `merge()` does, and never enters a rebuild loop:
# writing a second, differently specialized handle over an already-erased key is a rejected write, not
# a silent overwrite.
class Box[T]:
	pass


func test() -> void:
	var registry: Dictionary[Script, int] = {}
	registry.set(Box[int], 1)
	registry.set(Box[String], 2)
	print("not ok")
