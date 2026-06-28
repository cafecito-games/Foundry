abstract class Shape:
	abstract func area() -> float
final class Circle:
	var radius: float = 1.0
	final func describe() -> String:
		return "circle"
