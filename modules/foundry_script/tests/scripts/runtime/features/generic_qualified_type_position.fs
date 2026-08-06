# A type-argument list binds to the last name of a qualified type, so `Outer.Box[int]` names the same
# specialization in a parameter type, a return type, a variable annotation, a container element type,
# and an `is` test as it does in value position.
class Outer:
	class Box[T]:
		var value: T

		func _init(initial: T) -> void:
			value = initial

		func describe() -> String:
			return "box %s" % [value]

	enum Result[T, E]:
		Ok(value: T)
		Err(error: E)


func take(box: Outer.Box[int]) -> String:
	return box.describe()


func make() -> Outer.Box[String]:
	return Outer.Box[String].new("hello")


func total(boxes: Array[Outer.Box[int]]) -> int:
	var sum := 0
	for box in boxes:
		sum += box.value
	return sum


func classify(value: Outer.Result[int, String]) -> String:
	if value is Outer.Result[int, String].Ok(number):
		return "ok %d" % (number + 1)
	return "other"


func match_case(value: Outer.Result[int, String]) -> String:
	match value:
		Outer.Result[int, String].Ok(var number):
			return "case ok %d" % (number * 2)
		Outer.Result[int, String].Err(var message):
			return "case err %s" % message
	return "unreachable"


func test() -> void:
	var box: Outer.Box[int] = Outer.Box[int].new(7)
	print(take(box))
	print(make().describe())
	print(total([Outer.Box[int].new(2), Outer.Box[int].new(3)]))
	print(classify(Outer.Result[int, String].Ok(41)))
	print(match_case(Outer.Result[int, String].Err("bad")))
	print("qualified type position ok")
