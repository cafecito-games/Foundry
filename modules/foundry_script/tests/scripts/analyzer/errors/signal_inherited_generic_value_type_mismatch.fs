# A signal value read from a specialized receiver exposes the substituted signature, so the
# diagnostic renders `Signal[[int]]` rather than `Signal[[T]]` or a bare `Signal`.
class Base[T]:
	signal reported(value: T)


func test(base: Base[int]) -> void:
	var good: Signal[[int]] = base.reported
	var bad: Signal[[String]] = base.reported
	print(good, bad)
