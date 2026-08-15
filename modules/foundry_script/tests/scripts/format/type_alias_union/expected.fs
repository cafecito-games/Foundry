type Unsigned = uint | ulong
type Meters = float
type MaybeName = String? | int | StringName


class Inner:
	type Local = int | float

	func combine(left: int, right: int) -> int:
		return left | right


func test():
	var type = 1
	print(type | 2)
