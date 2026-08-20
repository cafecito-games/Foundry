# An untyped container literal commits to no alternative, since every alternative claims the same
# carrier, so it enters the union with nothing proved. The parameter verifies it when the call runs.
class Box:
	var label = "box"


class Receiver:
	func absorb(entries: Array[int] | Array[String]) -> void:
		print("absorbed ", entries)

	func drive() -> void:
		absorb([Box.new()])


func test() -> void:
	Receiver.new().drive()
