extends Node

class LocalHandle:
	pass

enum LocalEnum:
	VALUE = 0

# Nested Type[T] inside a closed Callable parameter list. Unclosed Callable parameter recovery
# is covered separately under bracketed_type_slots/.
var construct: Callable[[Type[No➡]], Node]
