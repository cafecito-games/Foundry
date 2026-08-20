# When every alternative needs `Self` resolved there is no alternative that can answer a value with no
# static type, so it is refused exactly as it is for a destination written as `Self` alone. Each
# alternative keeps the rules it would carry standing alone; the union around them adds none.
class Receiver:
	func drive(source) -> void:
		var soft = source if false else 5
		var every_alternative: (int, Self) | (String, Self) = soft
		var alone: Self = soft
		print(every_alternative, alone)


func test() -> void:
	pass
