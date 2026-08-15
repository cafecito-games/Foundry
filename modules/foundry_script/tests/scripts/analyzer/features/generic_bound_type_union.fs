# A union bound admits any argument that satisfies one of its alternatives. Explicitly written type
# arguments and inferred ones are checked by the same rule.
type Scalar = int | String
type NarrowInteger = int | long


class Square:
	pass


class Circle:
	pass


type EitherShape = Square | Circle


class Box[T: Scalar]:
	var value: T


class Pair[K: int | String, V: EitherShape]:
	var key: K


var integer_box: Box[int]
var text_box: Box[String]
var pair: Pair[String, Circle]
# Every alternative of the argument satisfies the bound, so the set as a whole does.
var either_box: Box[int | String]


func describe[T: Scalar](value: T) -> String:
	return str(value)


func describe_number[T: Number](value: T) -> String:
	return str(value)


func accept_either[T: EitherShape](shape: T) -> bool:
	return typeof(shape) == TYPE_OBJECT


func forward[U: NarrowInteger](value: U) -> String:
	# A type-parameter argument proves satisfaction through its own bound, never through erasure.
	return describe_number(value)


func test():
	prints(describe(1), describe("text"))
	prints(accept_either(Square.new()), accept_either(Circle.new()))
	print(forward(7))

	# Inference solves the parameter to the union itself, which the bound covers alternative by
	# alternative.
	var either: Scalar = 5
	print(describe(either))

	prints(integer_box, text_box, pair, either_box)
