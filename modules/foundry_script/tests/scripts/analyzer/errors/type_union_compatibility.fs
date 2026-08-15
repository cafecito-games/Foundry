# A concrete source needs one alternative that accepts it; a union source needs every alternative
# to satisfy the concrete target, because nothing narrows the value at the boundary.
class Left:
	var label: String = "left"


class Right:
	var label: String = "right"


type Either = Left | Right
type Scalar = int | uint


func take_string(value: String) -> String:
	return value


func test():
	var wrong: Scalar = "text"
	var either: Either = Left.new()
	var text: String = either
	var scalar: Scalar = 1
	prints(wrong, text, take_string(scalar))
