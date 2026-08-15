# `Number` is compiler-provided and globally visible: the closed set of the source-spellable numeric
# types (`int`, `uint`, `long`, `ulong`, `float`). It is usable wherever a written union is.
class Measure[T: Number]:
	var value: T


var counted: Measure[int]
var unsigned: Measure[uint]
var wide: Measure[long]
var wide_unsigned: Measure[ulong]
var real: Measure[float]
# Every alternative of a narrower numeric set is a member of `Number`.
var narrowed: Measure[int | long]


func identity[T: Number](value: T) -> T:
	return value


func widen[T: long | float](value: T) -> String:
	return str(value)


func test():
	prints(identity(1), identity(2U), identity(3L), identity(4UL), identity(5.5))
	prints(widen(6L), widen(7.5))

	var narrow: int | long = 8
	print(identity(narrow))

	prints(counted, unsigned, wide, wide_unsigned, real, narrowed)

	# An explicit type argument is a type position, so `Number` names the same set there.
	var explicit := Measure[Number].new()
	print(explicit != null)
