# `int` and `long` share the `Variant::INT` carrier and are told apart by their width descriptor. The
# runtime already distinguishes them, so the recorded evidence has to as well.
trait RchKeeper[T]:
	abstract func keep(item: T) -> T


class RchTarget:
	pass


extend RchTarget uses RchKeeper[int]:
	func keep(item: int) -> int:
		return item


func test() -> void:
	var conflicting: RchKeeper[long] = RchTarget.new()
	print(conflicting)
