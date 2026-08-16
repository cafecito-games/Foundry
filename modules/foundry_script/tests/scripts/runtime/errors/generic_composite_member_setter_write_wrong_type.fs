# A composite `Array[T]` member with a setter behaves exactly like a bare `T` member: the write is
# validated against the receiver's reified argument before the setter is entered, so none of the
# setter's side effects are observable for a value the member boundary refuses.
class Shelf[T]:
	var seen: Array = []
	var items: Array[T] = []:
		set(incoming):
			print("items setter ran")
			seen.append(incoming)
			items = incoming

	func stock(values) -> void:
		items = values


func test() -> void:
	var shelf := Shelf[int].new()
	var wrong: Array[String] = ["not an int"]
	shelf.stock(wrong)
	print(shelf.seen)
	print(shelf.items)
