# Removal runs downward, never upward: a value that does not fit `int` may still be a perfectly good
# `long`, so a failed `is int` rules out only `int`. `long` survives here, and the member lookup
# below reports that narrowed type.
type Scalar = int | long


func classify(value: Scalar) -> void:
	if value is not int:
		var probe := value.no_such_member
		print(probe)
