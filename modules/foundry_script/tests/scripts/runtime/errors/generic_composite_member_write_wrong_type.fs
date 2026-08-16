# A directly declared `Array[T]` member is validated against the instance's reified argument, so an
# array of the wrong element type is rejected at the member boundary instead of being laundered into
# the erased slot.
class Shelf[T]:
	var items: Array[T] = []

	func stock(values) -> void:
		items = values


func test() -> void:
	var shelf := Shelf[int].new()
	var wrong: Array[String] = ["not an int"]
	shelf.stock(wrong)
	print(shelf.items)
