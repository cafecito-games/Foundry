# A union bound is satisfied by an argument that satisfies one alternative, and by nothing else.
# `Number` is closed: belonging to a numeric-looking user type does not make a type numeric.
class Widget:
	pass


class Measure[T: Number]:
	var value: T


func identity[T: Number](value: T) -> T:
	return value


func forward[U](value: U) -> String:
	# An unbounded parameter proves nothing, so it cannot satisfy a concrete union bound.
	return str(identity(value))


var text: Measure[String]
var vector: Measure[Vector2]
var widget: Measure[Widget]
var anything: Measure[Variant]
# One alternative of the argument is outside the bound, so the set is not covered.
var mixed: Measure[int | String]


func test():
	print(identity("text"))
	print(forward(1))
	prints(text, vector, widget, anything, mixed)
