class_name LspTypeAliases

class Circle:
	var radius: float = 1.0

type Meters = float
type Scalar = int | uint
type Shape = Circle | Node

var distance: Meters = 0.0

func measure(scalar: Scalar) -> Meters:
	return 0.0

func paint(shape: Shape) -> void:
	pass

class Inner:
	func scale(value: Meters) -> Meters:
		return value
