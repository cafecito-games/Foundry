# An explicitly written `Variant` argument is an absence of evidence at that position, recorded and
# read exactly like a position the declaration left open. `RcvDuo[Variant, int]` therefore accepts
# every destination argument at the first position, while the concrete `int` sibling still rejects a
# destination that contradicts it.
trait RcvDuo[A, B]:
	abstract func label() -> String

	abstract func first() -> A

	abstract func accept(item: B) -> void


class RcvTarget:
	pass


extend RcvTarget uses RcvDuo[Variant, int]:
	func label() -> String:
		return "rcv"

	func first() -> Variant:
		return 3

	func accept(item: int) -> void:
		pass


func supply(value: Variant) -> Variant:
	return value


func reject_conflicting_known_position() -> void:
	var _slot: RcvDuo[int, String] = supply(RcvTarget.new())
	print("not reached: conflicting known position")


func test() -> void:
	var variant_string: RcvDuo[String, int] = supply(RcvTarget.new())
	print("Variant position accepts String: ", variant_string.label())
	var variant_float: RcvDuo[float, int] = supply(RcvTarget.new())
	print("Variant position accepts float: ", variant_float.label())
	reject_conflicting_known_position()
	print("done")
