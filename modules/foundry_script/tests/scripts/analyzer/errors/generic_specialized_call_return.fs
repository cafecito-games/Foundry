# Calling a generic method through a concrete specialization substitutes the return type, so
# the result is the bound argument (`int`) and cannot be stored in an incompatible variable.
class List[T]:
	var head: T

	func get_head() -> T:
		return head


class IntList extends List[int]:
	pass


func test() -> void:
	var list := IntList.new()
	var text: String = list.get_head()
	print(text)
