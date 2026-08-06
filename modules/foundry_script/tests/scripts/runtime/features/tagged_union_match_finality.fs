# A `match` that covers every case of a tagged union with unguarded, irrefutable patterns leaves no
# fallthrough, so a value-returning function needs no trailing unreachable `return`. Generic unions
# behave identically once specialized.
enum FinalitySignal:
	Idle
	Busy(amount: int)
	Failed(reason: String)

enum FinalityResult[T]:
	Ok(value: T)
	Err(error: String)

func describe(state: FinalitySignal) -> String:
	match state:
		FinalitySignal.Idle:
			return "idle"
		FinalitySignal.Busy(amount):
			return "busy %d" % amount
		FinalitySignal.Failed(reason):
			return "failed " + reason

func unwrap_or_zero(result: FinalityResult[int]) -> int:
	match result:
		FinalityResult[int].Ok(value):
			return value
		FinalityResult[int].Err(_):
			return 0

func label(flag: bool) -> String:
	match flag:
		true:
			return "on"
		false:
			return "off"

func test():
	print(describe(FinalitySignal.Idle))
	print(describe(FinalitySignal.Busy(4)))
	print(describe(FinalitySignal.Failed("disk")))

	print(unwrap_or_zero(FinalityResult[int].Ok(7)))
	print(unwrap_or_zero(FinalityResult[int].Err("nope")))

	print(label(true))
	print(label(false))
