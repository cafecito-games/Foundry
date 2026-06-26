# A typed container whose element is a specialized script handle (`Array[Box[int]]`) tracks the
# element's reified type arguments at runtime. A correctly specialized element is accepted; an
# unspecialized (raw) instance and a subclass whose specialization is not yet projectable onto the
# base are accepted under gradual typing. A conflicting same-script specialization is covered by the
# runtime/errors counterpart.
class Box[T]:
	var value: T

	func _init(v = null):
		value = v


class PairBox[A, B] extends Box[A]:
	var second: B


func make_int_box() -> Box[int]:
	return Box[int].new(1)


func make_raw_box() -> Variant:
	return Box.new(2)


func make_pair_box() -> Variant:
	return PairBox[int, float].new(3)


func test() -> void:
	var boxes: Array[Box[int]] = []

	# A matching specialization is accepted.
	boxes.append(make_int_box())
	print(boxes.size())

	# A raw, unspecialized instance carries no conflicting argument evidence and is accepted.
	@warning_ignore("unsafe_call_argument")
	boxes.append(make_raw_box())
	print(boxes.size())

	# A generic subclass specialized as `PairBox[int, float]` is a `Box[int]`; projecting its
	# specialization onto the base is deferred to epic #125, so it is accepted (not falsely rejected).
	@warning_ignore("unsafe_call_argument")
	boxes.append(make_pair_box())
	print(boxes.size())

	print(boxes[0].value)
	print(boxes[1].value)
	print(boxes[2].value)
	print("typed container element type arguments ok")
