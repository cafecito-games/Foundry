# A foreign Foundry Script root class is the target, so its lexical outer chain belongs to another
# parse tree entirely. The witness still reaches this file's own generic, in the return annotation and
# in the body, and still reads the target's own `power` member. The helper type is declared *after*
# the `extend` to prove the fallback obeys the same order-independent member resolution an ordinary
# class body does.
extend RtcScopeHolder uses RtcScopeCarrying:
	func carried() -> Carrier[int]:
		var made: Carrier[int] = Carrier[int].new()
		made.value = power
		return made


trait RtcScopeCarrying:
	abstract func carried() -> Carrier[int]


class Carrier[T]:
	var value: T

	func describe() -> String:
		return "carrier:" + str(value)


func test() -> void:
	var holder: RtcScopeCarrying = RtcScopeHolder.new()
	var carrier: Carrier[int] = holder.carried()
	print(carrier.describe())
