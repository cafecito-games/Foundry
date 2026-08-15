class Circle:
	var radius: float = 1.0

type Meters = float
type Shape = Circle | Node

var distance: Meters = 0.0

func measure() -> Meters:
	return distance

func paint(shape: Shape) -> void:
	pass
