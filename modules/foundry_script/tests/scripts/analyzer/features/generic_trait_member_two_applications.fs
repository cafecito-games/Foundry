# Two classes applying the same generic trait with different arguments each see their own
# specialization of the flattened members, and the trait's declaration is never rewritten.
trait Holder[T]:
	var value: T

	func store(item: T) -> void:
		value = item

	func get_value() -> T:
		return value


class IntBox uses Holder[int]:
	pass


class StringBox uses Holder[String]:
	pass


func test() -> void:
	var int_box := IntBox.new()
	int_box.store(7)
	var number: int = int_box.get_value()
	int_box.value = 9

	var string_box := StringBox.new()
	string_box.store("hello")
	var text: String = string_box.get_value()
	string_box.value = "world"

	print(number)
	print(text)
	print(int_box.value)
	print(string_box.value)
	print("generic trait member two applications ok")
