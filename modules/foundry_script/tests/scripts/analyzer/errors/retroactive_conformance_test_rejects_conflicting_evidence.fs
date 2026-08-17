# A statically known type test against contradicted evidence is an error, exactly as it is for a
# declared `uses`: the answer is already decided, so deferring it to run time would hide a mistake.
trait RceKeeper[T]:
	abstract func keep(item: T) -> T


class RceTarget:
	pass


extend RceTarget uses RceKeeper[int]:
	func keep(item: int) -> int:
		return item


extend int uses RceKeeper[int]:
	func keep(item: int) -> int:
		return item


func test() -> void:
	var value := RceTarget.new()
	print("class is: ", value is RceKeeper[String])
	var number := 3
	print("builtin is: ", number is RceKeeper[String])
	var cast_result := value as RceKeeper[String]
	print(cast_result)
