# Pins the outer-class edge of member specialization: a nested class that does not mention the outer
# class's type parameter needs no substitution and stays reachable through a specialized handle.
class Outer[T]:
	var value: T

	class Inner:
		var label: String = "inner"

		func describe() -> String:
			return label


func test() -> void:
	var inner := Outer[int].Inner.new()
	print(inner.describe())
	print("nested class in generic outer ok")
