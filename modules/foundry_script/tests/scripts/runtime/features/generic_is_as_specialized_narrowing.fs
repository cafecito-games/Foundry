# A successful specialized test narrows the tested value to that specialization, so the guarded body
# reads its members at the narrowed argument type. Only values whose runtime evidence really is that
# specialization may enter the branch: a mismatched specialization and a raw instance both take the
# fallback path instead of reaching a member read that assumes the narrowed type.
class Crate[T]:
	var item: T


class IntCrate extends Crate[int]:
	pass


func read_int(value: Variant) -> int:
	if value is Crate[int]:
		return value.item
	return -1


func read_string(value: Variant) -> String:
	if value is Crate[String]:
		return value.item
	return "<none>"


func test() -> void:
	var int_crate := Crate[int].new()
	int_crate.item = 7
	var string_crate := Crate[String].new()
	string_crate.item = "seven"
	var raw_crate := Crate.new()
	var fixed_leaf := IntCrate.new()
	fixed_leaf.item = 11

	print("int crate narrows: ", read_int(int_crate))
	print("string crate does not narrow to int: ", read_int(string_crate))
	print("raw crate does not narrow to int: ", read_int(raw_crate))
	print("fixed leaf narrows through its base: ", read_int(fixed_leaf))

	print("string crate narrows: ", read_string(string_crate))
	print("int crate does not narrow to String: ", read_string(int_crate))
	print("raw crate does not narrow to String: ", read_string(raw_crate))

	print("is/as specialized narrowing ok")
