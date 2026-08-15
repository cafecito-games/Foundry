# An alias that fails a generic parameter's bound in explicit type-argument position reports the
# bound failure -- naming its normalized members -- not the value-position "can only be used in a
# type position" diagnostic.
type Scalar = int | String
type Wrong = String | bool


class Measure[T: Scalar]:
	var value: T


func test():
	var measured := Measure[Wrong].new()
	print(measured)
