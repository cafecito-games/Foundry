# Outside its own declaration a generic tagged union is named by applying a full type-argument
# vector, in type position and in value position alike.
enum Result[T, E]:
	Ok(value: T)
	Err(error: E)

class Box[T]:
	var held: T

class Outer:
	enum Nested[T]:
		Value(value: T)

func typed(value: Result[int, String]) -> void:
	print(value)

func nested() -> void:
	var values: Array[Result[int, String]] = []
	print(values)

func nullable(value: Result[String?, int]) -> void:
	print(value)

func handle() -> void:
	var applied = Result[int, String]
	print(applied)

# An application is carried whole wherever another type argument accepts it, and a union reached
# through member access applies its arguments the same way a bare one does.
func nested_argument(box: Box[Result[int, String]]) -> void:
	print(box)

func nested_handle() -> void:
	var applied = Box[Result[int, String]]
	print(applied)

func qualified_handle() -> void:
	var applied = Outer.Nested[int]
	print(applied)
