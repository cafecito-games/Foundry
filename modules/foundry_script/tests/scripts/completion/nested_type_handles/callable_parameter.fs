extends Node

class LocalHandle:
	pass

enum LocalEnum:
	VALUE = 0

# A closed signature with a partially typed name, because two general completion behaviors
# would otherwise mask the position under test: an unclosed `Callable[[` reports the return
# type context, and a caret sitting directly on a closing bracket reports no context at all.
var construct: Callable[[Type[No➡]], Node]
