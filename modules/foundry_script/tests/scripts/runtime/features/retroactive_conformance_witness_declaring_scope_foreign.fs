# A foreign Foundry Script root class is the target, so its lexical outer chain belongs to another
# parse tree. The witness still reaches this file's own generic and named tuple, in the return
# annotation and in the body, and still reads the target's own `power` member. The helper types are
# declared *after* the `extend` to prove the fallback obeys the same order-independent member
# resolution an ordinary class body does.
extend RtcScopeHolder uses RtcScopeCarrying:
	func carried() -> Carrier[int]:
		var made: Carrier[int] = Carrier[int].new()
		made.value = power
		made.span = Span(power, power + 1)
		return made


trait RtcScopeCarrying:
	abstract func carried() -> Carrier[int]


tuple Span(low: int, high: int)


class Carrier[T]:
	var value: T
	var span: Span = Span(0, 0)

	func describe() -> String:
		return "carrier:" + str(value) + ":" + str(span.high)


func test() -> void:
	var holder: RtcScopeCarrying = RtcScopeHolder.new()
	var carrier: Carrier[int] = holder.carried()
	print(carrier.describe())
