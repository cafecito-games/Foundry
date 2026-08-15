# Same-carrier numeric alternatives cannot be told apart in general: every `int` value passes
# `is long` too. Testing the wider alternative first therefore consumes the narrower arm, and once
# removal is downward-closed the surviving set no longer holds anything the later test could match,
# so the chain is rejected instead of silently never running. Narrowest-first is the fix.
type Scalar = int | long | float | String


func widest_first(value: Scalar) -> String:
	if value is long:
		return "long"
	elif value is int:
		return "int"
	elif value is float:
		return "float"
	return "String"
