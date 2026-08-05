# Producer half of the cross-script fixed-width integer reflection fixture. Declares one member and
# one signature slot per public integer type, plus typed containers that share a carrier, so a
# consumer compiled from a separate file can be checked for exact width retention.
extends "res://runtime/features/fixed_width_integer_reflection_base.notest.fs"

var narrow_signed: int = 1
var narrow_unsigned: uint = 1U
var wide_signed: long = 1L
var wide_unsigned: ulong = 1UL
var narrow_signed_list: Array[int] = []
var wide_signed_list: Array[long] = []
var narrow_unsigned_map: Dictionary[uint, int] = {}
var wide_unsigned_map: Dictionary[ulong, long] = {}


func measure(narrow: int, wide: long) -> long:
	return wide + narrow


func measure_unsigned(narrow: uint, wide: ulong) -> ulong:
	return wide + narrow


func echo[T](value: T) -> T:
	return value


func overridable(value: uint) -> uint:
	return value + 1U
