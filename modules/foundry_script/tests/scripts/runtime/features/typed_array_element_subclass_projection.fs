# A subclass value whose specialization projects onto the expected base is accepted into a typed
# container. A non-generic `StringBox extends Box[String]` and a generic `PairBox[A, B] extends Box[A]`
# both validate against `Array[Box[String]]`/`Array[Box[int]]` when their projected base argument
# matches. The conflicting cases are covered by the runtime/errors counterparts.
class Box[T]:
	var value: T

	func _init(v = null):
		value = v


class StringBox extends Box[String]:
	pass


class PairBox[A, B] extends Box[A]:
	var second: B

	func _init(v = null):
		value = v


func make_pair_int_box() -> Variant:
	return PairBox[int, float].new(7)


func make_string_box() -> Variant:
	return StringBox.new("hello")


func make_pair_string_box() -> Variant:
	return PairBox[String, int].new("world")


func test() -> void:
	var ints: Array[Box[int]] = []
	@warning_ignore("unsafe_call_argument")
	ints.append(make_pair_int_box())
	print(ints.size())
	print(ints[0].value)

	var strings: Array[Box[String]] = []
	@warning_ignore("unsafe_call_argument")
	strings.append(make_string_box())
	@warning_ignore("unsafe_call_argument")
	strings.append(make_pair_string_box())
	print(strings.size())
	print(strings[0].value)
	print(strings[1].value)
	print("typed array element subclass projection ok")
