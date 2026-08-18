# The runtime record of a conformance agrees with its declaration-side record, position by position.
# `Duo[int, Self]` on a non-final target proves `int` at the first position, so a store that requires
# `String` there is rejected even when the value arrives as a `Variant`; the second position stayed
# open, so it accepts whatever the destination declares.
trait RcosDuo[A, B]:
	abstract func first() -> A

	abstract func accept(item: B) -> void


class RcosTarget:
	pass


extend RcosTarget uses RcosDuo[int, Self]:
	func first() -> int:
		return 3

	func accept(item: Self) -> void:
		pass


func supply(value: Variant) -> Variant:
	return value


func reject_conflicting_known_position() -> void:
	var _slot: RcosDuo[String, RcosTarget] = supply(RcosTarget.new())
	print("not reached: conflicting known position")


func test() -> void:
	var open_string: RcosDuo[int, String] = supply(RcosTarget.new())
	print("open position accepts String: ", open_string.first())
	var open_float: RcosDuo[int, float] = supply(RcosTarget.new())
	print("open position accepts float: ", open_float.first())
	reject_conflicting_known_position()
	print("done")
