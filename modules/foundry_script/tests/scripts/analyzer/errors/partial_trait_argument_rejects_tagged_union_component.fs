# A generic tagged union carries its type arguments where they can be compared and its case payload
# schema where they cannot. An unreified parameter in the payload does not erase the arguments beside
# it, so the fixed component still contradicts the destination while the agreeing form stays legal.
trait Keeper[T]:
	func label() -> String:
		return "keeper"


enum Result[A, B]:
	Ok(value: A)
	Err(error: B)


class Partial[U]:
	uses Keeper[Result[int, U]]


func bad(value: Partial) -> Keeper[Result[String, float]]:
	return value


func fine(value: Partial) -> Keeper[Result[int, float]]:
	return value


func test() -> void:
	print(bad(Partial[float].new()) != null)
	print(fine(Partial[float].new()) != null)
