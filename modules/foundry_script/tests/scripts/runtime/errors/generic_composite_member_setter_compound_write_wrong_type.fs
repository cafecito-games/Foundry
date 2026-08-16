# A compound assignment through a setter is validated on the operation result, at the same boundary a
# plain assignment's value is, so the concatenated array never reaches the setter.
class Shelf[T]:
	var seen: Array = []
	var items: Array[T] = []:
		set(incoming):
			print("items setter ran")
			seen.append(incoming)
			items = incoming

	func extend(values) -> void:
		items += values


func test() -> void:
	var shelf := Shelf[int].new()
	shelf.extend(["not an int"])
	print(shelf.seen)
	print(shelf.items)
