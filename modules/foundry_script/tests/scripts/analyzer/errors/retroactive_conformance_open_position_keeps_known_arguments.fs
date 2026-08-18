# A conformance that supplies a concrete argument beside an open one keeps the concrete position as
# evidence. `Duo[int, Self]` on a non-final target proves `int` at the first position, so a
# destination that declares `String` there is rejected; the second position stays open and rejects
# nothing.
trait RcoDuo[A, B]:
	abstract func first() -> A

	abstract func accept(item: B) -> void


class RcoTarget:
	pass


extend RcoTarget uses RcoDuo[int, Self]:
	func first() -> int:
		return 0

	func accept(item: Self) -> void:
		pass


func test() -> void:
	var value := RcoTarget.new()
	var agreeing: RcoDuo[int, String] = value
	var also_agreeing: RcoDuo[int, float] = value
	var conflicting: RcoDuo[String, RcoTarget] = value
	print(agreeing, also_agreeing, conflicting)
