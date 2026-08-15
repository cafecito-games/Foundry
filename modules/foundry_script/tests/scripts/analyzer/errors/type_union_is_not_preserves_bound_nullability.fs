# A type-parameter descriptor carries no nullability of its own; the bound does. A failed type test
# must therefore keep the bound's nullability, or a null the test never ruled out would be typed
# away. The member lookup below reports the surviving type, still nullable.
type MaybeScalar = int? | String


func classify[X: MaybeScalar](value: X) -> void:
	if value is not String:
		var probe := value.no_such_member
		print(probe)
