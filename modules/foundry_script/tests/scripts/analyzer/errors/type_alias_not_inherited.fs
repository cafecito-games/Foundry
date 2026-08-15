# Alias visibility is lexical, not nominal: a name declared in a class body belongs to that body and
# the bodies nested inside it. Inheriting the class does not carry the name along, which also keeps a
# generic base from handing out an alias written in its own unbound type-parameter frame.
class Base[T]:
	type Element = Array[T]
	type Meters = float


class Derived extends Base[int]:
	var values: Element = []
	var distance: Meters = 1.0


func test():
	var derived := Derived.new()
	prints(derived.values, derived.distance)
