# A generic subclass forwards a type parameter to its base (`PairBox[A, B] extends Box[A]`). The
# instance carries reified arguments against its OWN parameters, so the runtime projects them onto the
# base's parameter before the invariance check: `PairBox[String, float]` is a `Box[String]` and is
# rejected from an `Array[Box[int]]`. The funnel through `Variant` defeats the static check.
class Box[T]:
	var value: T


class PairBox[A, B] extends Box[A]:
	var second: B


func make_pair_box() -> Variant:
	return PairBox[String, float].new()


func test() -> void:
	var boxes: Array[Box[int]] = []
	boxes.append(make_pair_box())
	print("not ok")
