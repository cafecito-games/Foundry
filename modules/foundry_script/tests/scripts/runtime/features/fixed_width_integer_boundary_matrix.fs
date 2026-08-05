# These rows are deliberately data-like: source and compiled bytecode execute the same boundary
# values through every public width and through the language boundaries that must preserve them.
enum WideReading:
	BELOW_INT = -2147483649
	ABOVE_INT = 2147483648

class Holder:
	var signed_narrow: int = -2147483648
	var unsigned_narrow: uint = 4294967295U
	var signed_wide: long = -9223372036854775808L
	var unsigned_wide: ulong = 18446744073709551615UL

class Box[T]:
	var value: T

signal reported(i: int, u: uint, l: long, ul: ulong)

func identity[T](value: T) -> T:
	return value

func echo_uint(value: uint) -> uint:
	return value

func echo_int(value: int) -> int:
	return value

func echo_long(value: long) -> long:
	return value

func echo_ulong(value: ulong) -> ulong:
	return value

func increment_nullable(value: uint?) -> uint?:
	if value == null:
		return null
	return value + 1U

func increment_variant(value: Variant):
	if value is uint:
		return value + 1U
	return null

func test():
	var holder := Holder.new()
	print(holder.signed_narrow)
	print(holder.unsigned_narrow)
	print(holder.signed_wide)
	print(holder.unsigned_wide)

	# Parameters, returns, signals, and rich Callable signatures retain the uint descriptor.
	var int_callback: Callable[[int], int] = echo_int
	var uint_callback: Callable[[uint], uint] = echo_uint
	var long_callback: Callable[[long], long] = echo_long
	var ulong_callback: Callable[[ulong], ulong] = echo_ulong
	print(int_callback.call(2147483647))
	print(uint_callback.call(4294967295U))
	print(long_callback.call(9223372036854775807L))
	print(ulong_callback.call(18446744073709551615UL))
	reported.emit(2147483647, 4294967295U, 9223372036854775807L, 18446744073709551615UL)

	# Typed containers and reified generics recover their descriptor on reads.
	var ints: Array[int] = [-2147483648, 2147483647]
	var uints: Array[uint] = [0U, 4294967295U]
	var longs: Array[long] = [-9223372036854775808L, 9223372036854775807L]
	var ulongs: Array[ulong] = [0UL, 18446744073709551615UL]
	var lookup: Dictionary[int, long] = {-2147483648: -9223372036854775808L, 2147483647: 9223372036854775807L}
	var int_box := Box[int].new()
	var uint_box := Box[uint].new()
	var long_box := Box[long].new()
	var ulong_box := Box[ulong].new()
	int_box.value = 2147483647
	uint_box.value = 4294967295U
	long_box.value = 9223372036854775807L
	ulong_box.value = 18446744073709551615UL
	print(ints)
	print(uints)
	print(longs)
	print(ulongs)
	print(lookup[2147483647])
	print(identity[int](int_box.value))
	print(identity[uint](uint_box.value))
	print(identity[long](long_box.value))
	print(identity[ulong](ulong_box.value))

	# Variant erases the width but not the carrier. A type test against a width-declaring spelling is a
	# range test as well as a carrier test. The legacy `int` spelling declares no width yet, so it
	# still accepts the whole signed carrier and its two rows below read `true`.
	var erased_uint: Variant = 4294967296UL
	var erased_int: Variant = 2147483648L
	var below_int: Variant = -2147483649L
	var below_uint: Variant = -1L
	var above_long: Variant = 9223372036854775808UL
	print(erased_uint is uint)
	print(erased_uint is ulong)
	print(erased_int is int)
	print(erased_int is long)
	print(below_int is int)
	print(below_int is long)
	print(below_uint is uint)
	print(above_long is long)
	print(increment_nullable(41U))
	print(increment_variant(41U))

	# Classic enums stay signed 64-bit and continue to accept values outside public int's range.
	print(WideReading.BELOW_INT)
	print(WideReading.ABOVE_INT)
	var erased_enum: Variant = WideReading.ABOVE_INT
	print(typeof(erased_enum) == TYPE_INT)
	print(erased_enum is long)
