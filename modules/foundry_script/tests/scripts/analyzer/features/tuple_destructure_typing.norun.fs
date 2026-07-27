# Destructuring hands each binding the static type of the tuple element it reads, for both unnamed
# and named tuples, and a `_` slot simply skips its element.
tuple Vec2(x: float, y: float)

func unnamed() -> int:
	var (count, label) = (1, "one")
	var typed_count: int = count
	var typed_label: String = label
	return typed_count + typed_label.length()

func named(point: Vec2) -> float:
	var (horizontal, vertical) = point
	var typed_horizontal: float = horizontal
	return typed_horizontal + vertical

func skipped() -> String:
	var (_, middle, _) = (1, "middle", true)
	var typed_middle: String = middle
	return typed_middle

func immutable_bindings() -> int:
	const (first, second) = (2, 3)
	return first + second
