# Traversing a union's members brings every dimension `DataType::operator==` ignores into the
# invariant comparison, not just the parameter list of a callable alternative: the async marker, a
# typed rest tail, the return type, and the type arguments of a class alternative all separate two
# unions that would otherwise read as one type.
class Keeper[T]:
	var held: T


class Box[E]:
	pass


func take_async(k: Keeper[AsyncCallable[[int], void] | int]) -> void:
	print("async ", k)


func take_rest(k: Keeper[Callable[[...Array[int]], void] | int]) -> void:
	print("rest ", k)


func take_return(k: Keeper[Callable[[int], int] | String]) -> void:
	print("return ", k)


func take_nested(k: Keeper[Box[int] | int]) -> void:
	print("nested ", k)


func relay_async(k: Keeper[Callable[[int], void] | int]) -> void:
	take_async(k)


func relay_rest(k: Keeper[Callable[[...Array[String]], void] | int]) -> void:
	take_rest(k)


func relay_return(k: Keeper[Callable[[int], String] | String]) -> void:
	take_return(k)


func relay_nested(k: Keeper[Box[String] | int]) -> void:
	take_nested(k)


func test() -> void:
	print("skipped")
