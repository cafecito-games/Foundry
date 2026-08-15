# Narrowing a bounded type parameter refines the value, not the parameter. Inside the branch `value`
# is an `int` -- which the member lookup below reports -- while `X` itself is untouched, so every
# substitution and return check outside the branch still sees `X`.
func passthrough[X: Number](value: X) -> X:
	if value is int:
		var probe := value.no_such_member
		print(probe)
	return value
