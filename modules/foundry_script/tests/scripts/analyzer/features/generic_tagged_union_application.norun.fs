# Outside its own declaration a generic tagged union is named by applying a full type-argument
# vector, in type position and in value position alike.
enum Result[T, E]:
	Ok(value: T)
	Err(error: E)

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
