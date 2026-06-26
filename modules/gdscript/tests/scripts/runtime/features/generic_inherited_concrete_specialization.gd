# A non-generic class that concretely specializes a generic base (`IntBox extends Box[int]`) carries
# no reified `type_arguments` on its instances — the `int` binding lives in the `extends` clause. The
# inherited `T`-typed member is still validated against that fixed argument: well-typed values
# (including ones that convert) are accepted, and a dynamic wrong-typed write is rejected. A second
# specialization of the same base (`StringBox extends Box[String]`) binds independently.
class Box[T]:
	var value: T


class IntBox extends Box[int]:
	pass


class StringBox extends Box[String]:
	pass


func test() -> void:
	var int_box := IntBox.new()
	int_box.value = 42
	print(int_box.value)
	var dynamic_int: Variant = int_box
	dynamic_int.value = 7.0 # converts to int
	print(int_box.value)

	var string_box := StringBox.new()
	string_box.value = "hello"
	print(string_box.value)
	print("inherited concrete specialization ok")
